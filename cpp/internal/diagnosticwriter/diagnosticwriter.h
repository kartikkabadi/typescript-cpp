#pragma once

// diagnosticwriter — diagnosticwriter.go:1-610
//
// Pretty diagnostic formatting (colors, gutter, code frame), error summary,
// and watch-mode status writers.

#include <cstdint>
#include <iosfwd>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "internal/ast/ast.h"
#include "internal/core/types.h"
#include "internal/diagnostics/diagnostics.h"
#include "internal/locale/locale.h"
#include "internal/tspath/tspath.h"

namespace tsc::diagnosticwriter {

// FileLike — diagnosticwriter.go:21
class FileLike {
public:
    virtual ~FileLike() = default;
    virtual std::string_view fileName() const = 0;
    virtual std::string_view text() const = 0;
    virtual const std::vector<TextPos>& ecmaLineMap() const = 0;
};

// Diagnostic — diagnosticwriter.go:28. Abstracts over ast::Diagnostic and
// LSP diagnostics.
class Diagnostic {
public:
    virtual ~Diagnostic() = default;
    virtual const FileLike* file() = 0;
    virtual int pos() = 0;
    virtual int end() = 0;
    virtual int len() = 0;
    virtual int32_t code() = 0;
    virtual DiagnosticCategory category() = 0;
    // Source is a custom prefix shown before the code instead of "TS" (empty
    // means "TS"). A non-empty value marks the diagnostic as coming from an
    // external source (e.g. a content mapper).
    virtual std::string_view source() = 0;
    virtual std::string localize(const locale::Locale& locale) = 0;
    // Returned chains are owned by this Diagnostic (valid while it lives).
    virtual std::vector<Diagnostic*> messageChain() = 0;
    virtual std::vector<Diagnostic*> relatedInformation() = 0;
};

// resolvedLocation — diagnosticwriter.go:87
struct ResolvedLocation {
    TextRange loc;
    bool useOriginal = false; // render against the file's original, untransformed text
    bool synthesized = false; // the range is in virtual code with no corresponding original location
};

// ASTDiagnostic — diagnosticwriter.go:44
class ASTDiagnostic : public Diagnostic {
public:
    explicit ASTDiagnostic(tsc::Diagnostic* d) : d_(d) {}

    tsc::Diagnostic* diagnostic() const { return d_; }

    const FileLike* file() override;
    int pos() override { return resolve().loc.pos(); }
    int end() override { return resolve().loc.end(); }
    int len() override { return resolve().loc.len(); }
    int32_t code() override { return d_->code; }
    DiagnosticCategory category() override { return d_->category; }
    std::string_view source() override { return d_->source; }
    std::string localize(const locale::Locale& locale) override;
    std::vector<Diagnostic*> messageChain() override;
    std::vector<Diagnostic*> relatedInformation() override;

    // resolve determines where and against which text a diagnostic should be
    // reported — diagnosticwriter.go:97.
    ResolvedLocation resolve() const;

    // displayMessageArgs — ast/diagnostic.go:134
    std::vector<std::string> displayMessageArgs() const;

private:
    tsc::Diagnostic* d_;
    // Allocated FileLike wrappers and child diagnostics (Go allocates
    // per-call; identity of FileLike objects matters for map keys, so
    // wrappers are not cached across calls).
    std::vector<std::unique_ptr<FileLike>> fileLikes_;
    std::vector<std::unique_ptr<Diagnostic>> children_;
    std::vector<std::unique_ptr<tsc::Diagnostic>> ownedDiagnostics_;
};

ASTDiagnostic* wrapASTDiagnostic(tsc::Diagnostic* d);
std::vector<std::unique_ptr<ASTDiagnostic>> wrapASTDiagnostics(std::span<tsc::Diagnostic* const> diags);
std::vector<std::unique_ptr<Diagnostic>> fromASTDiagnostics(std::span<tsc::Diagnostic* const> diags);

// ToDiagnostics — diagnosticwriter.go:184
template <class T>
std::vector<Diagnostic*> toDiagnostics(std::span<T* const> diags) {
    std::vector<Diagnostic*> result;
    result.reserve(diags.size());
    for (auto* d : diags) {
        result.push_back(d);
    }
    return result;
}

// CompareASTDiagnostics — diagnosticwriter.go:192
int compareASTDiagnostics(ASTDiagnostic* a, ASTDiagnostic* b);

// FormattingOptions — diagnosticwriter.go:196
struct FormattingOptions {
    locale::Locale locale{};
    tspath::ComparePathsOptions comparePathsOptions{};
    std::string newLine = "\n";
};

// Color constants — diagnosticwriter.go:202
inline constexpr std::string_view foregroundColorEscapeGrey = "\x1b[90m";
inline constexpr std::string_view foregroundColorEscapeRed = "\x1b[91m";
inline constexpr std::string_view foregroundColorEscapeYellow = "\x1b[93m";
inline constexpr std::string_view foregroundColorEscapeBlue = "\x1b[94m";
inline constexpr std::string_view foregroundColorEscapeCyan = "\x1b[96m";

// diagnosticwriter.go:210
inline constexpr std::string_view gutterStyleSequence = "\x1b[7m";
inline constexpr std::string_view gutterSeparator = " ";
inline constexpr std::string_view resetEscapeSequence = "\x1b[0m";
inline constexpr std::string_view ellipsis = "...";

// FormattedWriter — diagnosticwriter.go:401
using FormattedWriter = void (*)(std::ostream& output, std::string_view text,
                                 std::string_view formatStyle);

void formatDiagnosticsWithColorAndContext(std::ostream& output,
                                          std::span<Diagnostic* const> diags,
                                          const FormattingOptions* formatOpts);
void formatDiagnosticWithColorAndContext(std::ostream& output,
                                         Diagnostic& diagnostic,
                                         const FormattingOptions* formatOpts);
std::string flattenDiagnosticMessage(Diagnostic& d, std::string_view newLine,
                                     const locale::Locale& locale);
void writeFlattenedASTDiagnosticMessage(std::ostream& writer,
                                        tsc::Diagnostic* diagnostic,
                                        std::string_view newline,
                                        const locale::Locale& locale);
void writeFlattenedDiagnosticMessage(std::ostream& writer,
                                     Diagnostic& diagnostic,
                                     std::string_view newline,
                                     const locale::Locale& locale);
void writeLocation(std::ostream& output, const FileLike* file, int pos,
                   const FormattingOptions* formatOpts,
                   FormattedWriter writeWithStyleAndReset);
void writeWithStyleAndReset(std::ostream& output, std::string_view text,
                            std::string_view formatStyle);

// ErrorSummary — diagnosticwriter.go:427
struct ErrorSummary {
    int totalErrorCount = 0;
    std::vector<Diagnostic*> globalErrors;
    std::unordered_map<const FileLike*, std::vector<Diagnostic*>> errorsByFile;
    std::vector<const FileLike*> sortedFiles;
};

void writeErrorSummaryText(std::ostream& output,
                           std::span<Diagnostic* const> allDiagnostics,
                           const FormattingOptions* formatOpts);
void writeFormatDiagnostics(std::ostream& output,
                            std::span<Diagnostic* const> diagnostics,
                            const FormattingOptions* formatOpts);
void writeFormatDiagnostic(std::ostream& output, Diagnostic& diagnostic,
                           const FormattingOptions* formatOpts);
void formatDiagnosticsStatusWithColorAndTime(std::ostream& output,
                                             std::string_view time,
                                             Diagnostic& diag,
                                             const FormattingOptions* formatOpts);
void formatDiagnosticsStatusAndTime(std::ostream& output,
                                    std::string_view time,
                                    Diagnostic& diag,
                                    const FormattingOptions* formatOpts);

// ScreenStartingCodes — diagnosticwriter.go:596
extern const std::vector<int32_t> kScreenStartingCodes;

// TryClearScreen — diagnosticwriter.go:601
bool tryClearScreen(std::ostream& output, Diagnostic& diag,
                    const CompilerOptions& options);

} // namespace tsc::diagnosticwriter
