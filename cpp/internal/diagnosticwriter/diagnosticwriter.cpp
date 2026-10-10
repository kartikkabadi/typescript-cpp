// diagnosticwriter — diagnosticwriter.go:1-610

#include "internal/diagnosticwriter/diagnosticwriter.h"

#include <algorithm>
#include <cstdio>
#include <ostream>
#include <sstream>
#include <unordered_map>

#include "internal/ast/diagnostics_util.h"
#include "internal/core/text.h"
#include "internal/diagnostics/messages_generated.h"
#include "internal/scanner/scanner.h"
#include "internal/spanmap/spanmap.h"
#include "internal/stringutil/stringutil.h"

namespace tsc::diagnosticwriter {

namespace {

// sourceFileLike adapts *SourceFile to FileLike (Go's *ast.SourceFile
// implements FileLike directly). Adapters are interned per SourceFile so
// that, like Go, the same underlying file has a single FileLike identity.
class SourceFileLike : public FileLike {
public:
    explicit SourceFileLike(SourceFile* file) : file_(file) {}

    std::string_view fileName() const override { return file_->FileName(); }
    std::string_view text() const override { return file_->Text(); }
    const std::vector<TextPos>& ecmaLineMap() const override {
        return file_->ecmaLineMap();
    }
    SourceFile* asSourceFile() const override { return file_; }

private:
    SourceFile* file_;
};

const FileLike* sourceFileLikeFor(SourceFile* file) {
    // Adapter memo keyed by file — Go returns the *ast.SourceFile itself
    // (it implements FileLike). Parallel `tsc -b` workers format
    // diagnostics concurrently, so guard the cache; the map is leaked
    // deliberately like Go's GC-held values (detached workers' tails
    // must never hit an exit-time destructor).
    static std::mutex adaptersMu;
    static auto* adapters =
        new std::unordered_map<SourceFile*, SourceFileLike>();
    std::lock_guard<std::mutex> lk(adaptersMu);
    auto [it, inserted] = adapters->try_emplace(file, file);
    return &it->second;
}

// originalTextFile presents a source file's original (untransformed) text as
// a FileLike — diagnosticwriter.go:114
class OriginalTextFile : public FileLike {
public:
    // newOriginalTextFile — diagnosticwriter.go:120
    OriginalTextFile(SourceFile* file, std::string_view fileName)
        : fileName_(fileName), text_(file->OriginalText()),
          lineMap_(computeECMALineStarts(text_)) {}

    std::string_view fileName() const override { return fileName_; }
    std::string_view text() const override { return text_; }
    const std::vector<TextPos>& ecmaLineMap() const override { return lineMap_; }

private:
    std::string_view fileName_;
    std::string_view text_;
    std::vector<TextPos> lineMap_;
};

// renamedFile — diagnosticwriter.go:134
class RenamedFile : public FileLike {
public:
    RenamedFile(SourceFile* file, std::string_view fileName)
        : file_(file), fileName_(fileName) {}

    std::string_view fileName() const override { return fileName_; }
    std::string_view text() const override { return file_->Text(); }
    const std::vector<TextPos>& ecmaLineMap() const override {
        return file_->ecmaLineMap();
    }

private:
    SourceFile* file_;
    std::string_view fileName_;
};

// FileLike-based replicas of the scanner helpers (the scanner takes a
// concrete SourceFile*, while here we only have a FileLike).
int getECMALineOfPosition(const FileLike* file, int pos) {
    return computeLineOfPosition(file->ecmaLineMap(), pos);
}

// scanner.GetECMALineAndUTF16CharacterOfPosition — scanner.go:2698
std::pair<int, int> getECMALineAndUTF16CharacterOfPosition(const FileLike* file,
                                                         int pos) {
    const auto& lineMap = file->ecmaLineMap();
    int line = computeLineOfPosition(lineMap, pos);
    int character = utf16Len(
        std::string_view(file->text()).substr(lineMap[line], pos - lineMap[line]));
    return {line, character};
}

// scanner.GetECMAPositionOfLineAndByteOffset — scanner.go:2725
int getECMAPositionOfLineAndByteOffset(const FileLike* file, int line,
                                       int byteOffset) {
    return computePositionOfLineAndByteOffset(file->ecmaLineMap(), line,
                                              byteOffset);
}

// unicode.IsSpace — the exact set Go's unicode package uses.
bool isUnicodeSpace(char32_t c) {
    switch (c) {
    case '\t': case '\n': case '\v': case '\f': case '\r': case ' ':
    case 0x85: case 0xA0:
        return true;
    }
    switch (c) {
    case 0x1680:
    case 0x2000: case 0x2001: case 0x2002: case 0x2003: case 0x2004:
    case 0x2005: case 0x2006: case 0x2007: case 0x2008: case 0x2009:
    case 0x200A:
    case 0x2028: case 0x2029:
    case 0x202F:
    case 0x205F:
    case 0x3000:
        return true;
    }
    return false;
}

// strings.TrimRightFunc(text, unicode.IsSpace)
std::string trimRightSpace(std::string_view s) {
    size_t end = s.size();
    size_t pos = 0;
    while (pos < s.size()) {
        int width;
        char32_t ch = decodeUtf8Rune(s.substr(pos), &width);
        if (width == 0) break;
        if (!isUnicodeSpace(ch)) {
            end = pos + width;
        }
        pos += width;
    }
    return std::string(s.substr(0, end));
}

// diagnosticPrefix returns the prefix shown before a diagnostic's code, e.g.
// "TS" for compiler diagnostics or a content mapper's custom source for its
// diagnostics — diagnosticwriter.go:391
std::string_view diagnosticPrefix(Diagnostic& diagnostic) {
    if (auto source = diagnostic.source(); !source.empty()) {
        return source;
    }
    return "TS";
}

// getCategoryFormat — diagnosticwriter.go:397
std::string_view getCategoryFormat(DiagnosticCategory category) {
    switch (category) {
    case DiagnosticCategory::Error:
        return foregroundColorEscapeRed;
    case DiagnosticCategory::Warning:
        return foregroundColorEscapeYellow;
    case DiagnosticCategory::Suggestion:
        return foregroundColorEscapeGrey;
    case DiagnosticCategory::Message:
        return foregroundColorEscapeBlue;
    }
    tscUnreachable("Unhandled diagnostic category");
}

void writeCodeSnippet(std::ostream& writer, const FileLike* sourceFile,
                      int start, int length, std::string_view squiggleColor,
                      std::string_view indent,
                      const FormattingOptions* formatOpts) {
    auto [firstLine, firstLineChar] =
        getECMALineAndUTF16CharacterOfPosition(sourceFile, start);
    auto [lastLine, lastLineChar] =
        getECMALineAndUTF16CharacterOfPosition(sourceFile, start + length);
    if (length == 0) {
        lastLineChar++; // When length is zero, squiggle the character right after the start position.
    }

    int lastLineOfFile =
        getECMALineOfPosition(sourceFile, (int)sourceFile->text().size());

    bool hasMoreThanFiveLines = lastLine - firstLine >= 4;
    int gutterWidth = (int)std::to_string(lastLine + 1).size();
    if (hasMoreThanFiveLines) {
        gutterWidth = std::max((int)ellipsis.size(), gutterWidth);
    }

    for (int i = firstLine; i <= lastLine; i++) {
        writer << formatOpts->newLine;

        // If the error spans over 5 lines, we'll only show the first 2 and last 2 lines,
        // so we'll skip ahead to the second-to-last line.
        if (hasMoreThanFiveLines && firstLine + 1 < i && i < lastLine - 1) {
            writer << indent;
            writer << gutterStyleSequence;
            // %*s pads left with spaces to gutterWidth
            writer << std::string(gutterWidth - (int)ellipsis.size() > 0
                                      ? gutterWidth - (int)ellipsis.size()
                                      : 0,
                                  ' ')
                   << ellipsis;
            writer << resetEscapeSequence;
            writer << gutterSeparator;
            writer << formatOpts->newLine;
            i = lastLine - 1;
        }

        int lineStart = getECMAPositionOfLineAndByteOffset(sourceFile, i, 0);
        int lineEnd;
        if (i < lastLineOfFile) {
            lineEnd = getECMAPositionOfLineAndByteOffset(sourceFile, i + 1, 0);
        } else {
            lineEnd = (int)sourceFile->text().size();
        }

        std::string lineContent = trimRightSpace(
            std::string_view(sourceFile->text()).substr(lineStart, lineEnd - lineStart)); // trim from end
        std::replace(lineContent.begin(), lineContent.end(), '\t',
                     ' '); // convert tabs to single spaces

        // Output the gutter and the actual contents of the line.
        writer << indent;
        writer << gutterStyleSequence;
        // %*d — right-justified line number
        {
            std::string lineNo = std::to_string(i + 1);
            if ((int)lineNo.size() < gutterWidth) {
                writer << std::string(gutterWidth - (int)lineNo.size(), ' ');
            }
            writer << lineNo;
        }
        writer << resetEscapeSequence;
        writer << gutterSeparator;
        writer << lineContent;
        writer << formatOpts->newLine;

        // Output the gutter and the error span for the line using tildes.
        writer << indent;
        writer << gutterStyleSequence;
        writer << std::string(gutterWidth, ' ');
        writer << resetEscapeSequence;
        writer << gutterSeparator;
        writer << squiggleColor;
        if (i == firstLine) {
            // If we're on the last line, then limit it to the last character of the last line.
            // Otherwise, we'll just squiggle the rest of the line, giving 'slice' no end position.
            int lastCharForLine;
            if (i == lastLine) {
                lastCharForLine = (int)lastLineChar;
            } else {
                lastCharForLine = (int)utf16Len(lineContent);
            }

            // Fill with spaces until the first character,
            // then squiggle the remainder of the line.
            writer << std::string((int)firstLineChar, ' ');
            writer << std::string(lastCharForLine - (int)firstLineChar, '~');
        } else if (i == lastLine) {
            // Squiggle until the final character.
            writer << std::string((int)lastLineChar, '~');
        } else {
            // Squiggle the entire line.
            writer << std::string((int)utf16Len(lineContent), '~');
        }

        writer << resetEscapeSequence;
    }
}

void flattenDiagnosticMessageChain(std::ostream& writer, Diagnostic& chain,
                                   std::string_view newLine,
                                   const locale::Locale& locale, int level) {
    writer << newLine;
    for (int i = 0; i < level; i++) {
        writer << "  ";
    }

    writer << chain.localize(locale);
    for (Diagnostic* child : chain.messageChain()) {
        flattenDiagnosticMessageChain(writer, *child, newLine, locale,
                                      level + 1);
    }
}

// ErrorSummary helpers — diagnosticwriter.go:482
ErrorSummary getErrorSummary(std::span<Diagnostic* const> diags) {
    ErrorSummary summary;

    for (Diagnostic* diagnostic : diags) {
        if (diagnostic->category() != DiagnosticCategory::Error) {
            continue;
        }

        summary.totalErrorCount++;
        if (diagnostic->file() == nullptr) {
            summary.globalErrors.push_back(diagnostic);
        } else {
            // Go calls File() per map access; each call produces a fresh
            // FileLike for wrapped files (the same map-key behavior).
            summary.errorsByFile[diagnostic->file()].push_back(diagnostic);
        }
    }

    // !!!
    // Need an ordered map here, but sorting for consistency.
    for (const auto& [file, _] : summary.errorsByFile) {
        summary.sortedFiles.push_back(file);
    }
    std::sort(summary.sortedFiles.begin(), summary.sortedFiles.end(),
              [](const FileLike* a, const FileLike* b) {
                  return a->fileName().compare(b->fileName()) < 0;
              });

    return summary;
}

std::string prettyPathForFileError(const FileLike* file,
                                   const std::vector<Diagnostic*>& fileErrors,
                                   const FormattingOptions* formatOpts) {
    if (file == nullptr || fileErrors.empty()) {
        return "";
    }
    int line = getECMALineOfPosition(file, fileErrors[0]->pos());
    std::string fileName(file->fileName());
    if (tspath::pathIsAbsolute(fileName) &&
        tspath::pathIsAbsolute(formatOpts->comparePathsOptions.currentDirectory)) {
        fileName =
            tspath::convertToRelativePath(file->fileName(),
                                        formatOpts->comparePathsOptions);
    }
    std::string result;
    result += fileName;
    result += foregroundColorEscapeGrey;
    result += ":" + std::to_string(line + 1);
    result += resetEscapeSequence;
    return result;
}

void writeTabularErrorsDisplay(std::ostream& output,
                               const ErrorSummary& errorSummary,
                               const FormattingOptions* formatOpts) {
    const auto& sortedFiles = errorSummary.sortedFiles;

    size_t maxErrors = 0;
    for (const auto& [_, errorsForFile] : errorSummary.errorsByFile) {
        maxErrors = std::max(maxErrors, errorsForFile.size());
    }

    // !!!
    // TODO (drosen): This was never localized.
    // Should make this better.
    std::string headerRow =
        localize(formatOpts->locale, Errors_Files, "", {});
    int leftColumnHeadingLength =
        (int)headerRow.substr(0, headerRow.find(' ')).size();
    int lengthOfBiggestErrorCount =
        (int)std::to_string(maxErrors).size();
    int leftPaddingGoal =
        std::max(leftColumnHeadingLength, lengthOfBiggestErrorCount);
    int headerPadding =
        std::max(lengthOfBiggestErrorCount - leftColumnHeadingLength, 0);

    output << std::string(headerPadding, ' ');
    output << headerRow;
    output << formatOpts->newLine;

    for (const FileLike* file : sortedFiles) {
        // sortedFiles only contains keys of errorsByFile.
        const std::vector<Diagnostic*>& fileErrors =
            errorSummary.errorsByFile.at(file);
        int errorCount = (int)fileErrors.size();

        // %*d  — right-justified count followed by two spaces
        {
            std::string countStr = std::to_string(errorCount);
            if ((int)countStr.size() < leftPaddingGoal) {
                output << std::string(leftPaddingGoal - (int)countStr.size(),
                                      ' ');
            }
            output << countStr << "  ";
        }
        output << prettyPathForFileError(file, fileErrors, formatOpts);
        output << formatOpts->newLine;
    }
}

} // namespace

// File — diagnosticwriter.go:55
const FileLike* ASTDiagnostic::file() {
    SourceFile* file = d_->file;
    if (file == nullptr) {
        return nullptr;
    }
    std::string_view fileName = file->FileName();
    if (SourceFile* canonical = file->CanonicalSourceFile();
        canonical != nullptr) {
        fileName = canonical->FileName();
    }
    if (resolve().useOriginal) {
        // The mapper's own diagnostics (Source != "") already carry original ranges; compiler
        // diagnostics have their transformed ranges mapped back. Both render against the original,
        // untransformed text. Diagnostics in synthesized code (see resolve) keep the virtual text.
        fileLikes_.push_back(
            std::make_unique<OriginalTextFile>(file, fileName));
        return fileLikes_.back().get();
    }
    if (fileName != file->FileName()) {
        fileLikes_.push_back(std::make_unique<RenamedFile>(file, fileName));
        return fileLikes_.back().get();
    }
    return sourceFileLikeFor(file);
}

// resolve determines where and against which text a diagnostic should be reported. A content mapper's
// own diagnostics already carry original ranges. A compiler diagnostic on a content-mapped file has its
// virtual range mapped back to the original; if it falls entirely within synthesized code, there is no
// original location, so it is shown against the virtual text and flagged as synthesized.
// diagnosticwriter.go:97
ResolvedLocation ASTDiagnostic::resolve() const {
    TextRange loc = d_->loc;
    SourceFile* file = d_->file;
    if (file == nullptr) {
        return ResolvedLocation{loc, false, false};
    }
    if (!d_->source.empty()) {
        return ResolvedLocation{loc, true, false};
    }
    if (file->SpanMap() != nullptr) {
        auto [mapped, fidelity] =
            spanmap::VirtualToOriginalSpan(file->SpanMap(), loc);
        if (fidelity == spanmap::FidelityNone) {
            return ResolvedLocation{loc, false, true};
        }
        return ResolvedLocation{mapped, true, false};
    }
    return ResolvedLocation{loc, false, false};
}

// displayMessageArgs substitutes the original text for a complete alias span when a diagnostic argument
// exactly matches the virtual alias. Stored arguments remain unchanged for code fixes and serialization.
// ast/diagnostic.go:134
std::vector<std::string> ASTDiagnostic::displayMessageArgs() const {
    if (d_->file == nullptr || !d_->source.empty()) {
        return d_->messageArgs;
    }
    auto [segment, ok] =
        spanmap::AliasForVirtualSpan(d_->file->SpanMap(), d_->loc);
    if (!ok) {
        return d_->messageArgs;
    }
    std::string_view virtualText = d_->file->Text();
    std::string_view originalText = d_->file->OriginalText();
    if (segment.VirtualStart < 0 ||
        segment.VirtualEnd > (TextPos)virtualText.size() ||
        segment.OriginalStart < 0 ||
        segment.OriginalEnd > (TextPos)originalText.size()) {
        return d_->messageArgs;
    }
    std::string_view virtualName = virtualText.substr(
        segment.VirtualStart, segment.VirtualEnd - segment.VirtualStart);
    std::string_view originalName = originalText.substr(
        segment.OriginalStart, segment.OriginalEnd - segment.OriginalStart);
    std::vector<std::string> result;
    bool cloned = false;
    for (size_t i = 0; i < d_->messageArgs.size(); i++) {
        if (d_->messageArgs[i] != virtualName) {
            continue;
        }
        if (!cloned) {
            result = d_->messageArgs;
            cloned = true;
        }
        result[i] = originalName;
    }
    if (cloned) {
        return result;
    }
    return d_->messageArgs;
}

// Localize — ast/diagnostic.go:117
std::string ASTDiagnostic::localize(const locale::Locale& loc) {
    if (d_->message == nullptr && !d_->messageText.empty()) {
        return d_->messageText;
    }
    return tsc::localize(loc, d_->message, std::string(d_->messageKey),
                         displayMessageArgs());
}

// RelatedInformation — diagnosticwriter.go:49
std::vector<Diagnostic*> ASTDiagnostic::relatedInformation() {
    std::vector<Diagnostic*> result;
    result.reserve(d_->relatedInformation.size());
    for (auto* r : d_->relatedInformation) {
        children_.push_back(std::make_unique<ASTDiagnostic>(r));
        result.push_back(children_.back().get());
    }
    return result;
}

// MessageChain — diagnosticwriter.go:145
std::vector<Diagnostic*> ASTDiagnostic::messageChain() {
    std::vector<Diagnostic*> result;
    result.reserve(d_->messageChain.size() + 1);
    for (auto* c : d_->messageChain) {
        children_.push_back(std::make_unique<ASTDiagnostic>(c));
        result.push_back(children_.back().get());
    }
    if (resolve().synthesized) {
        // The diagnostic points into synthesized virtual code; make clear the shown location is not in the
        // original file, and which content mapper produced it.
        ownedDiagnostics_.push_back(std::unique_ptr<tsc::Diagnostic>(
            newDetachedDiagnostic(
                TextRange::undefined(),
                This_location_is_in_virtual_code_produced_by_the_content_mapper_0_and_has_no_corresponding_location_in_the_original_file,
                {d_->file->ContentMapper()})));
        children_.push_back(std::make_unique<ASTDiagnostic>(
            ownedDiagnostics_.back().get()));
        result.push_back(children_.back().get());
    }
    return result;
}

ASTDiagnostic* wrapASTDiagnostic(tsc::Diagnostic* d) {
    return new ASTDiagnostic(d);
}

std::vector<std::unique_ptr<ASTDiagnostic>> wrapASTDiagnostics(
    std::span<tsc::Diagnostic* const> diags) {
    std::vector<std::unique_ptr<ASTDiagnostic>> result;
    result.reserve(diags.size());
    for (auto* d : diags) {
        result.push_back(std::unique_ptr<ASTDiagnostic>(wrapASTDiagnostic(d)));
    }
    return result;
}

std::vector<std::unique_ptr<Diagnostic>> fromASTDiagnostics(
    std::span<tsc::Diagnostic* const> diags) {
    std::vector<std::unique_ptr<Diagnostic>> result;
    result.reserve(diags.size());
    for (auto* d : diags) {
        result.push_back(std::unique_ptr<Diagnostic>(wrapASTDiagnostic(d)));
    }
    return result;
}

// CompareASTDiagnostics — diagnosticwriter.go:192
int compareASTDiagnostics(ASTDiagnostic* a, ASTDiagnostic* b) {
    return CompareDiagnostics(a->diagnostic(), b->diagnostic());
}

// FormatDiagnosticsWithColorAndContext — diagnosticwriter.go:218
void formatDiagnosticsWithColorAndContext(
    std::ostream& output, std::span<Diagnostic* const> diags,
    const FormattingOptions* formatOpts) {
    if (diags.empty()) {
        return;
    }
    for (size_t i = 0; i < diags.size(); i++) {
        if (i > 0) {
            output << formatOpts->newLine;
        }
        formatDiagnosticWithColorAndContext(output, *diags[i], formatOpts);
    }
}

// FormatDiagnosticWithColorAndContext — diagnosticwriter.go:230
void formatDiagnosticWithColorAndContext(
    std::ostream& output, Diagnostic& diagnostic,
    const FormattingOptions* formatOpts) {
    if (diagnostic.file() != nullptr) {
        const FileLike* file = diagnostic.file();
        int pos = diagnostic.pos();
        writeLocation(output, file, pos, formatOpts, writeWithStyleAndReset);
        output << " - ";
    }

    writeWithStyleAndReset(output, categoryName(diagnostic.category()),
                           getCategoryFormat(diagnostic.category()));
    // "%s %s%d: %s"
    output << foregroundColorEscapeGrey << ' ' << diagnosticPrefix(diagnostic)
           << diagnostic.code() << ": " << resetEscapeSequence;
    writeFlattenedDiagnosticMessage(output, diagnostic, formatOpts->newLine,
                                    formatOpts->locale);

    if (diagnostic.file() != nullptr &&
        diagnostic.code() != File_appears_to_be_binary->code) {
        output << formatOpts->newLine;
        writeCodeSnippet(output, diagnostic.file(), diagnostic.pos(),
                         diagnostic.len(), getCategoryFormat(diagnostic.category()),
                         "", formatOpts);
        output << formatOpts->newLine;
    }

    auto related = diagnostic.relatedInformation();
    if (!related.empty()) {
        for (Diagnostic* relatedInformation : related) {
            const FileLike* file = relatedInformation->file();
            if (file != nullptr) {
                output << formatOpts->newLine;
                output << "  ";
                int pos = relatedInformation->pos();
                writeLocation(output, file, pos, formatOpts,
                              writeWithStyleAndReset);
                output << " - ";
                writeFlattenedDiagnosticMessage(output, *relatedInformation,
                                                formatOpts->newLine,
                                                formatOpts->locale);
                writeCodeSnippet(output, file, pos, relatedInformation->len(),
                                 foregroundColorEscapeCyan, "    ", formatOpts);
            }
            output << formatOpts->newLine;
        }
    }
}

// FlattenDiagnosticMessage — diagnosticwriter.go:347
std::string flattenDiagnosticMessage(Diagnostic& d, std::string_view newLine,
                                     const locale::Locale& locale) {
    std::ostringstream output;
    writeFlattenedDiagnosticMessage(output, d, newLine, locale);
    return output.str();
}

// WriteFlattenedASTDiagnosticMessage — diagnosticwriter.go:353
void writeFlattenedASTDiagnosticMessage(std::ostream& writer,
                                        tsc::Diagnostic* diagnostic,
                                        std::string_view newline,
                                        const locale::Locale& locale) {
    std::unique_ptr<ASTDiagnostic> wrapped(wrapASTDiagnostic(diagnostic));
    writeFlattenedDiagnosticMessage(writer, *wrapped, newline, locale);
}

// WriteFlattenedDiagnosticMessage — diagnosticwriter.go:357
void writeFlattenedDiagnosticMessage(std::ostream& writer,
                                     Diagnostic& diagnostic,
                                     std::string_view newline,
                                     const locale::Locale& locale) {
    writer << diagnostic.localize(locale);

    for (Diagnostic* chain : diagnostic.messageChain()) {
        flattenDiagnosticMessageChain(writer, *chain, newline, locale,
                                      1 /*level*/);
    }
}

// writeWithStyleAndReset — diagnosticwriter.go:405
void writeWithStyleAndReset(std::ostream& output, std::string_view text,
                            std::string_view formatStyle) {
    output << formatStyle;
    output << text;
    output << resetEscapeSequence;
}

// WriteLocation — diagnosticwriter.go:411
void writeLocation(std::ostream& output, const FileLike* file, int pos,
                   const FormattingOptions* formatOpts,
                   FormattedWriter writeWithStyleAndReset) {
    auto [firstLine, firstChar] =
        getECMALineAndUTF16CharacterOfPosition(file, pos);
    std::string relativeFileName;
    if (formatOpts != nullptr) {
        relativeFileName = tspath::convertToRelativePath(
            file->fileName(), formatOpts->comparePathsOptions);
    } else {
        relativeFileName = std::string(file->fileName());
    }

    writeWithStyleAndReset(output, relativeFileName,
                           foregroundColorEscapeCyan);
    output << ":";
    writeWithStyleAndReset(output, std::to_string(firstLine + 1),
                           foregroundColorEscapeYellow);
    output << ":";
    writeWithStyleAndReset(output, std::to_string(firstChar + 1),
                           foregroundColorEscapeYellow);
}

// WriteErrorSummaryText — diagnosticwriter.go:435. Roughly corresponds to
// 'getErrorSummaryText' from watch.ts.
void writeErrorSummaryText(std::ostream& output,
                           std::span<Diagnostic* const> allDiagnostics,
                           const FormattingOptions* formatOpts) {
    ErrorSummary errorSummary = getErrorSummary(allDiagnostics);
    int totalErrorCount = errorSummary.totalErrorCount;
    if (totalErrorCount == 0) {
        return;
    }

    const FileLike* firstFile = nullptr;
    if (!errorSummary.sortedFiles.empty()) {
        firstFile = errorSummary.sortedFiles[0];
    }
    std::vector<Diagnostic*> firstFileErrors;
    if (auto it = errorSummary.errorsByFile.find(firstFile);
        it != errorSummary.errorsByFile.end()) {
        firstFileErrors = it->second;
    }
    std::string firstFileName =
        prettyPathForFileError(firstFile, firstFileErrors, formatOpts);
    int numErroringFiles = (int)errorSummary.errorsByFile.size();

    std::string message;
    if (totalErrorCount == 1) {
        // Special-case a single error.
        if (!errorSummary.globalErrors.empty() || firstFileName.empty()) {
            message =
                localize(formatOpts->locale, Found_1_error, "", {});
        } else {
            message = localize(formatOpts->locale, Found_1_error_in_0, "",
                               {firstFileName});
        }
    } else {
        switch (numErroringFiles) {
        case 0:
            // No file-specific errors.
            message = localize(formatOpts->locale, Found_0_errors, "",
                               {std::to_string(totalErrorCount)});
            break;
        case 1:
            // One file with errors.
            message = localize(
                formatOpts->locale,
                Found_0_errors_in_the_same_file_starting_at_Colon_1, "",
                {std::to_string(totalErrorCount), firstFileName});
            break;
        default:
            // Multiple files with errors.
            message = localize(formatOpts->locale, Found_0_errors_in_1_files,
                               "",
                               {std::to_string(totalErrorCount),
                                std::to_string(numErroringFiles)});
            break;
        }
    }
    output << formatOpts->newLine;
    output << message;
    output << formatOpts->newLine;
    output << formatOpts->newLine;
    if (numErroringFiles > 1) {
        writeTabularErrorsDisplay(output, errorSummary, formatOpts);
        output << formatOpts->newLine;
    }
}

// WriteFormatDiagnostics — diagnosticwriter.go:565
void writeFormatDiagnostics(std::ostream& output,
                            std::span<Diagnostic* const> diagnostics,
                            const FormattingOptions* formatOpts) {
    for (Diagnostic* diagnostic : diagnostics) {
        writeFormatDiagnostic(output, *diagnostic, formatOpts);
    }
}

// WriteFormatDiagnostic — diagnosticwriter.go:571
void writeFormatDiagnostic(std::ostream& output, Diagnostic& diagnostic,
                           const FormattingOptions* formatOpts) {
    if (diagnostic.file() != nullptr) {
        auto [line, character] = getECMALineAndUTF16CharacterOfPosition(
            diagnostic.file(), diagnostic.pos());
        std::string fileName(diagnostic.file()->fileName());
        std::string relativeFileName = tspath::convertToRelativePath(
            fileName, formatOpts->comparePathsOptions);
        output << relativeFileName << "(" << line + 1 << "," << character + 1
               << "): ";
    }

    output << categoryName(diagnostic.category()) << ' '
           << diagnosticPrefix(diagnostic) << diagnostic.code() << ": ";
    writeFlattenedDiagnosticMessage(output, diagnostic, formatOpts->newLine,
                                    formatOpts->locale);
    output << formatOpts->newLine;
}

// FormatDiagnosticsStatusWithColorAndTime — diagnosticwriter.go:584
void formatDiagnosticsStatusWithColorAndTime(
    std::ostream& output, std::string_view time, Diagnostic& diag,
    const FormattingOptions* formatOpts) {
    output << "[";
    writeWithStyleAndReset(output, time, foregroundColorEscapeGrey);
    output << "] ";
    writeFlattenedDiagnosticMessage(output, diag, formatOpts->newLine,
                                    formatOpts->locale);
}

// FormatDiagnosticsStatusAndTime — diagnosticwriter.go:591
void formatDiagnosticsStatusAndTime(std::ostream& output, std::string_view time,
                                    Diagnostic& diag,
                                    const FormattingOptions* formatOpts) {
    output << time << " - ";
    writeFlattenedDiagnosticMessage(output, diag, formatOpts->newLine,
                                    formatOpts->locale);
}

// ScreenStartingCodes — diagnosticwriter.go:596
const std::vector<int32_t> kScreenStartingCodes = {
    Starting_compilation_in_watch_mode->code,
    File_change_detected_Starting_incremental_compilation->code,
};

// TryClearScreen — diagnosticwriter.go:601
bool tryClearScreen(std::ostream& output, Diagnostic& diag,
                    const CompilerOptions& options) {
    if (!tristateIsTrue(options.PreserveWatchOutput) &&
        !tristateIsTrue(options.ExtendedDiagnostics) &&
        !tristateIsTrue(options.Diagnostics) &&
        std::find(kScreenStartingCodes.begin(), kScreenStartingCodes.end(),
                  diag.code()) != kScreenStartingCodes.end()) {
        output << "\x1B[2J\x1B[3J\x1B[H"; // Clear screen and move cursor to home position
        return true;
    }
    return false;
}

} // namespace tsc::diagnosticwriter
