// Port of tsc/internal/ls/lsconv/converters_test.go.
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#include "internal/win32/w32compat.h"
#else
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#endif

#include "internal/ast/ast.h"
#include "internal/core/types.h"
#include "internal/gostd/testing.h"
#include "internal/ls/lsconv/lsconv.h"
#include "internal/ls/lsdeps.h"
#include "internal/lsp/lsproto/lsproto.h"
#include "internal/parser/parser.h"
#include "internal/spanmap/spanmap.h"
#include "internal/testutil/testutil.h"
#include "internal/testutil/unittests/registry.h"

using tsc::gostd::testing::T;
using namespace tsc;
namespace lsconv = tsc::lsconv;
namespace lsproto = tsc::lsp::lsproto;
namespace spanmap = tsc::spanmap;

static void TestDocumentURIToFileName(T* t) {
	t->Parallel();

	struct {
		std::string uri;
		std::string fileName;
	} tests[] = {
	    {"file:///path/to/file.ts", "/path/to/file.ts"},
	    {"file://server/share/file.ts", "//server/share/file.ts"},
	    {"file:///d%3A/work/tsgo932/lib/utils.ts",
	     "d:/work/tsgo932/lib/utils.ts"},
	    {"file:///D%3A/work/tsgo932/lib/utils.ts",
	     "D:/work/tsgo932/lib/utils.ts"},
	    {"file:///d%3A/work/tsgo932/app/%28test%29/comp/comp-test.tsx",
	     "d:/work/tsgo932/app/(test)/comp/comp-test.tsx"},
	    {"file:///path/to/file.ts#section", "/path/to/file.ts"},
	    {"file:///c:/test/me", "c:/test/me"},
	    {"file://shares/files/c%23/p.cs", "//shares/files/c#/p.cs"},
	    {"file:///c:/Source/Z%C3%BCrich%20or%20Zurich%20(%CB%88zj%CA%8A%C9%99r%C9%AAk,/Code/resources/app/plugins/c%23/plugin.json",
	     "c:/Source/Zürich or Zurich (ˈzjʊərɪk,/Code/resources/app/plugins/c#/plugin.json"},
	    {"file:///c:/test %25/path", "c:/test %/path"},
	    // {"file:?q", "/"},
	    {"file:///_:/path", "/_:/path"},
	    {"file:///users/me/c%23-projects/", "/users/me/c#-projects/"},
	    {"file://localhost/c%24/GitDevelopment/express",
	     "//localhost/c$/GitDevelopment/express"},
	    {"file:///c%3A/test%20with%20%2525/c%23code",
	     "c:/test with %25/c#code"},

	    {"untitled:Untitled-1",
	     "^/~ts-uri~/untitled/ts-nul-authority/Untitled-1"},
	    {"untitled:Untitled-1#fragment",
	     "^/~ts-uri~/untitled/ts-nul-authority/~ts-uri-escape~556e7469746c65642d310023667261676d656e74~"},
	    {"untitled:c:/Users/jrieken/Code/abc.txt",
	     "^/~ts-uri~/untitled/ts-nul-authority/~ts-uri-escape~633a~/Users/jrieken/Code/abc.txt"},
	    {"untitled:C:/Users/jrieken/Code/abc.txt",
	     "^/~ts-uri~/untitled/ts-nul-authority/~ts-uri-escape~433a~/Users/jrieken/Code/abc.txt"},
	    {"untitled://wsl%2Bubuntu/home/jabaile/work/TypeScript/newfile.ts",
	     "^/~ts-uri~/untitled/wsl%2Bubuntu/home/jabaile/work/TypeScript/newfile.ts"},
	};

	for (auto& test : tests) {
		t->Run(test.uri, [test](T* t) {
			t->Parallel();
			gotest::assert::Equal(
			    t,
			    lsproto::documentUriFileName(lsproto::DocumentUri(test.uri)),
			    test.fileName);
		});
	}
}
REGISTER_UNIT_TEST("ls/lsconv.TestDocumentURIToFileName",
                   TestDocumentURIToFileName);

// converters_test.go:96 — non-file URIs round-trip through the normalized
// dynamic file name.
static void
TestNonFileDocumentURIRoundTripsThroughNormalizedFileName(T* t) {
	t->Parallel();

	gotest::assert::Equal(
	    t,
	    lsproto::documentUriFileName(lsproto::DocumentUri(
	        "custom:folder/../~ts-uri~/caf\xC3\xA9\\file.ts")),
	    "^/~ts-uri~/custom/ts-nul-authority/folder/~ts-uri-escape~2e2e~/~ts-uri~/~ts-uri-escape~636166c3a95c66696c65~.ts");
	gotest::assert::Equal(
	    t,
	    lsproto::documentUriFileName(lsproto::DocumentUri(
	        "custom:~ts-uri-escape~dir.js/file.ts?x=1")),
	    "^/~ts-uri~/custom/ts-nul-authority/~ts-uri-escape~7e74732d7572692d6573636170657e6469722e6a73~/~ts-uri-escape~66696c65003f783d31~.ts");

	for (std::string_view uri :
	     {"untitled:folder/../file.ts",
	      "vscode-vfs://github/path//file.ts", "custom:/path/./file.ts/",
	      "custom:", "custom:///path", "custom://authority",
	      "custom://authority/", "custom:path/file.ts?rev=a/b#frag/c",
	      "custom://authority/path/file.ts#frag/a",
	      "custom:path\\file.ts", "custom:.git/file.ts",
	      "custom:..hidden/file.ts", "custom://~ts-uri~/path",
	      "custom://ts-nul-authority/path",
	      "custom:~ts-uri-escape~file.ts",
	      "custom:~ts-uri-escape~no-path",
	      "custom://authority/~ts-uri-no-path~~",
	      "custom:~ts-uri-spec~666f6f~/file.ts?x=1",
	      "custom:folder/../~ts-uri~/caf\xC3\xA9\\file.ts",
	      "custom:name.ts\\", "custom:name..ts"}) {
		t->Run(std::string(uri), [uri](T* t) {
			t->Parallel();
			auto fileName = lsproto::documentUriFileName(
			    lsproto::DocumentUri(uri));
			gotest::assert::Equal(
			    t, lsconv::FileNameToDocumentURI(fileName),
			    lsproto::DocumentUri(uri));
		});
	}

	for (std::string_view uri :
	     {"custom:path\\file.ts", "custom:~ts-uri~file.ts",
	      "custom:~ts-uri-escape~file.ts"}) {
		auto fileName = lsproto::documentUriFileName(
		    lsproto::DocumentUri(uri));
		gotest::assert::Equal(
		    t, tspath::tryGetExtensionFromPath(fileName),
		    tspath::extensionTs);
	}

	std::string literalDynamicFileName =
	    "^/custom/ts-nul-authority/~ts-uri-escape~666f6f~.ts";
	gotest::assert::Equal(
	    t, lsconv::FileNameToDocumentURI(literalDynamicFileName),
	    lsproto::DocumentUri("custom:~ts-uri-escape~666f6f~.ts"));

	std::string invalidUTF8FileName =
	    "^/~ts-uri~/custom/ts-nul-authority/~ts-uri-escape~ff~";
	gotest::assert::Equal(
	    t, lsconv::FileNameToDocumentURI(invalidUTF8FileName),
	    lsproto::DocumentUri("custom:~ts-uri-escape~ff~"));

	gotest::assert::Assert(
	    t,
	    lsproto::documentUriFileName(
	        lsproto::DocumentUri("custom:name.ts\\")) !=
	        lsproto::documentUriFileName(
	            lsproto::DocumentUri("custom:name..ts")));
	gotest::assert::Assert(
	    t, lsproto::documentUriFileName(lsproto::DocumentUri(
	           "custom:~ts-uri-escape~types.d.css.ts"))
	           .ends_with(".d.css.ts"));
}
REGISTER_UNIT_TEST(
    "ls/lsconv.TestNonFileDocumentURIRoundTripsThroughNormalizedFileName",
    TestNonFileDocumentURIRoundTripsThroughNormalizedFileName);

static void TestFileNameToDocumentURI(T* t) {
	t->Parallel();

	struct {
		std::string fileName;
		std::string uri;
	} tests[] = {
	    {"/path/to/file.ts", "file:///path/to/file.ts"},
	    {"//server/share/file.ts", "file://server/share/file.ts"},
	    {"d:/work/tsgo932/lib/utils.ts",
	     "file:///d%3A/work/tsgo932/lib/utils.ts"},
	    {"D:/work/tsgo932/lib/utils.ts",
	     "file:///d%3A/work/tsgo932/lib/utils.ts"},
	    {"d:/work/tsgo932/app/(test)/comp/comp-test.tsx",
	     "file:///d%3A/work/tsgo932/app/%28test%29/comp/comp-test.tsx"},
	    {"/path/to/file.ts", "file:///path/to/file.ts"},
	    {"c:/test/me", "file:///c%3A/test/me"},
	    {"//shares/files/c#/p.cs", "file://shares/files/c%23/p.cs"},
	    {"c:/Source/Zürich or Zurich (ˈzjʊərɪk,/Code/resources/app/plugins/c#/plugin.json",
	     "file:///c%3A/Source/Z%C3%BCrich%20or%20Zurich%20%28%CB%88zj%CA%8A%C9%99r%C9%AAk%2C/Code/resources/app/plugins/c%23/plugin.json"},
	    {"c:/test %/path", "file:///c%3A/test%20%25/path"},
	    {"/", "file:///"},
	    {"/_:/path", "file:///_%3A/path"},
	    {"/users/me/c#-projects/", "file:///users/me/c%23-projects/"},
	    {"//localhost/c$/GitDevelopment/express",
	     "file://localhost/c%24/GitDevelopment/express"},
	    {"c:/test with %25/c#code",
	     "file:///c%3A/test%20with%20%2525/c%23code"},

	    {"^/untitled/ts-nul-authority/Untitled-1", "untitled:Untitled-1"},
	    {"^/untitled/ts-nul-authority/c:/Users/jrieken/Code/abc.txt",
	     "untitled:c:/Users/jrieken/Code/abc.txt"},
	    {"^/untitled/ts-nul-authority///wsl%2Bubuntu/home/jabaile/work/TypeScript/newfile.ts",
	     "untitled://wsl%2Bubuntu/home/jabaile/work/TypeScript/newfile.ts"},
	};

	for (auto& test : tests) {
		t->Run(test.fileName, [test](T* t) {
			t->Parallel();
			gotest::assert::Equal(
			    t,
			    std::string(
			        lsconv::FileNameToDocumentURI(test.fileName)),
			    test.uri);
		});
	}
}
REGISTER_UNIT_TEST("ls/lsconv.TestFileNameToDocumentURI",
                   TestFileNameToDocumentURI);

// testScript implements the lsconv `Script` concept for plain-text tests.
struct testScript {
	std::string name;
	std::string text;
	std::string originalText;
	spanmap::SpanMap* spanMap = nullptr;

	const std::string& FileName() const { return name; }
	const std::string& OriginalFileName() const { return name; }
	const std::string& Text() const { return text; }
	const std::string& OriginalText() const {
		if (!originalText.empty()) {
			return originalText;
		}
		return text;
	}
	spanmap::SpanMap* SpanMap() const { return spanMap; }
};

static std::pair<lsconv::Converters*, testScript*>
newTestConverters(const std::string& text) {
	auto* script = new testScript{.name = "test.ts", .text = text};
	auto* lineMap = lsconv::ComputeLSPLineStarts(text);
	auto* conv = lsconv::NewConverters(
	    lsproto::PositionEncodingKindUTF16,
	    [lineMap](const std::string&) -> lsconv::LSPLineMap* {
		    return lineMap;
	    });
	return {conv, script};
}

static void TestConvertersSourceFileProjectionExpansion(T* t) {
	t->Parallel();
	std::string original = "x";
	SourceFileParseOptions parseOptions;
	parseOptions.FileName = "/component.vue";
	parseOptions.Path = "/component.vue";
	auto* canonical =
	    tsc::parseSourceFile(parseOptions, " x", ScriptKind::TS);
	auto supplementalOptions = parseOptions;
	supplementalOptions.Path = "/component.vue::supplemental";
	auto* supplemental = tsc::parseSourceFile(supplementalOptions,
	                                           "  x", ScriptKind::TS);
	ContentMapperSourceFileInfo canonicalInfo;
	canonicalInfo.OriginalText = original;
	canonicalInfo.ContentMapper = "mapper";
	canonicalInfo.SpanMap = spanmap::New(
	    {spanmap::Segment{/*.VirtualStart = */ TextPos(1),
	                      /*.VirtualEnd = */ TextPos(2),
	                      /*.OriginalStart = */ TextPos(0),
	                      /*.OriginalEnd = */ TextPos(1),
	                      /*.Kind = */ spanmap::KindVerbatim,
	                      /*.Features = */ spanmap::FeatureAll}});
	canonicalInfo.SupplementalSourceFiles = {supplemental};
	canonical->SetContentMapperInfo(canonicalInfo);
	ContentMapperSourceFileInfo supplementalInfo;
	supplementalInfo.OriginalText = original;
	supplementalInfo.ContentMapper = "mapper";
	supplementalInfo.SpanMap = spanmap::New(
	    {spanmap::Segment{TextPos(2), TextPos(3), TextPos(0), TextPos(1),
	                      spanmap::KindVerbatim, spanmap::FeatureAll}});
	supplementalInfo.CanonicalSourceFile = canonical;
	supplemental->SetContentMapperInfo(supplementalInfo);
	auto* lineMap = lsconv::ComputeLSPLineStarts(original);
	auto* converters = lsconv::NewConverters(
	    lsproto::PositionEncodingKindUTF16,
	    [lineMap](const std::string&) -> lsconv::LSPLineMap* {
		    return lineMap;
	    });

	auto positions = converters->FromLSPPositionForSourceFile(
	    canonical, lsproto::Position{}, spanmap::FeatureHover);
	gotest::assert::Equal(t, int(positions.size()), 2);
	SourceFile* projectedFile = positions[0].Script;
	gotest::assert::Assert(t, projectedFile == canonical);
	gotest::assert::Assert(t, positions[0].Script == canonical);
	gotest::assert::Equal(t, positions[0].Position, TextPos(1));
	gotest::assert::Assert(t, positions[1].Script == supplemental);
	gotest::assert::Equal(t, positions[1].Position, TextPos(2));
}
REGISTER_UNIT_TEST("ls/lsconv.TestConvertersSourceFileProjectionExpansion",
                   TestConvertersSourceFileProjectionExpansion);

// TestConvertersInvalidUTF8 verifies behavior on text containing invalid UTF-8
// sequences (e.g. lone continuation bytes). Node's TextDecoder substitutes such
// bytes with U+FFFD, so the JS-reference test cannot cover this; we assert the
// expected Go-side behavior directly. Each invalid byte advances the byte
// position by 1 and the UTF-16 character by 1 (RuneError = 1 code unit).
static void TestConvertersInvalidUTF8(T* t) {
	t->Parallel();

	// Text with invalid UTF-8 byte 0x80 (continuation byte without start byte).
	// Old code used utf8.RuneLen(RuneError)==3, overshooting the byte offset.
	std::string text = "a\x80"
	                   "b\ncd";
	auto [conv, script] = newTestConverters(text);

	// (line, char) → byte position. Each row asserts both directions where the
	// position lies on a character boundary.
	struct {
		uint32_t line, ch;
		TextPos bytePos;
	} mappings[] = {
	    {0, 0, TextPos(0)}, // 'a'
	    {0, 1, TextPos(1)}, // invalid byte 0x80
	    {0, 2, TextPos(2)}, // 'b'
	    {0, 3, TextPos(3)}, // newline (line end)
	    {1, 0, TextPos(4)}, // 'c'
	    {1, 1, TextPos(5)}, // 'd'
	    {1, 2, TextPos(6)}, // EOF
	};
	for (auto& m : mappings) {
		lsproto::Position lc{.Line = m.line, .Character = m.ch};
		auto positions = lsconv::convertersFromLSPPosition(
		    conv, script, lc, spanmap::FeatureAll);
		gotest::assert::Equal(t, int(positions.size()), 1);
		gotest::assert::Equal(
		    t, positions[0].Position, m.bytePos,
		    gostd::sprintf("LineAndCharacterToPosition(%d,%d)",
		                   {int(m.line), int(m.ch)}));
		auto [lspPosition, _f] = conv->ToLSPPosition(script, m.bytePos);
		gotest::assert::Equal(
		    t, lspPosition, lc,
		    gostd::sprintf("PositionToLineAndCharacter(%d)",
		                   {int(m.bytePos)}));
	}

	// Byte-by-byte round-trip across the entire text.
	for (int bytePos = 0; bytePos <= int(text.size()); bytePos++) {
		auto [lc, _f] = conv->ToLSPPosition(script, TextPos(bytePos));
		auto positions = lsconv::convertersFromLSPPosition(
		    conv, script, lc, spanmap::FeatureAll);
		gotest::assert::Equal(t, int(positions.size()), 1);
		gotest::assert::Equal(
		    t, positions[0].Position, TextPos(bytePos),
		    gostd::sprintf("round-trip byte %d", {bytePos}));
	}
}
REGISTER_UNIT_TEST("ls/lsconv.TestConvertersInvalidUTF8",
                   TestConvertersInvalidUTF8);

// jsReferenceScript is a Node.js script that, given a list of UTF-8 byte buffers,
// computes the authoritative mapping between (line, character in UTF-16 code units)
// and UTF-8 byte offsets. See the Go test for the full description of the
// length-prefixed stdin protocol and the JSON output shape.
static const char* jsReferenceScript = R"JS(
const inChunks = [];
process.stdin.on('data', c => inChunks.push(c));
process.stdin.on('end', () => {
  const buf = Buffer.concat(inChunks);
  let off = 0;
  const readU32 = () => { const v = buf.readUInt32LE(off); off += 4; return v; };
  const n = readU32();
  const buffers = [];
  for (let i = 0; i < n; i++) {
    const len = readU32();
    buffers.push(buf.subarray(off, off + len));
    off += len;
  }

  const decoder = new TextDecoder('utf-8', { fatal: true });
  const out = buffers.map(bytes => {
    const text = decoder.decode(bytes);

    const lineStartsJs = [0];
    for (let i = 0; i < text.length; i++) {
      const c = text.charCodeAt(i);
      if (c === 13) {
        if (i + 1 < text.length && text.charCodeAt(i + 1) === 10) i++;
        lineStartsJs.push(i + 1);
      } else if (c === 10) {
        lineStartsJs.push(i + 1);
      }
    }

    const boundaries = [{ bytePos: 0, jsIdx: 0 }];
    let bytePos = 0, jsIdx = 0;
    while (bytePos < bytes.length) {
      const seq = utf8SeqLen(bytes[bytePos]);
      const cp = text.codePointAt(jsIdx);
      bytePos += seq;
      jsIdx += cp > 0xFFFF ? 2 : 1;
      boundaries.push({ bytePos, jsIdx });
    }

    return boundaries.map(({ bytePos, jsIdx }) => {
      let lo = 0, hi = lineStartsJs.length - 1;
      while (lo < hi) {
        const mid = (lo + hi + 1) >> 1;
        if (lineStartsJs[mid] <= jsIdx) lo = mid;
        else hi = mid - 1;
      }
      return { bytePos, line: lo, char: jsIdx - lineStartsJs[lo] };
    });
  });

  process.stdout.write(JSON.stringify(out));
});

function utf8SeqLen(b) {
  if (b < 0x80) return 1;
  if ((b & 0xE0) === 0xC0) return 2;
  if ((b & 0xF0) === 0xE0) return 3;
  if ((b & 0xF8) === 0xF0) return 4;
  throw new Error('invalid UTF-8 lead byte 0x' + b.toString(16));
}
)JS";

struct jsTuple {
	int BytePos = 0;
	int Line = 0;
	int Char = 0;
};

static bool nodeAvailable() {
	// exec.LookPath("node")
#ifdef _WIN32
	// PATHEXT + `;`-separated PATH, like Go's LookPath on windows.
	auto [resolved, err] = w32::lookPath("node", {});
	return err == 0;
#else
	const char* pathEnv = getenv("PATH");
	if (pathEnv == nullptr) return false;
	std::string path = pathEnv;
	size_t pos = 0;
	while (true) {
		size_t colon = path.find(':', pos);
		std::string dir =
		    path.substr(pos, colon == std::string::npos
		                     ? std::string::npos : colon - pos);
		if (dir.empty()) dir = ".";
		std::string candidate = dir + "/node";
		if (access(candidate.c_str(), X_OK) == 0) return true;
		if (colon == std::string::npos) break;
		pos = colon + 1;
	}
	return false;
#endif
}

// runJSReference runs the Node oracle over `texts` — see the Go test for the
// wire format. Skips the test when node is not on PATH.
static std::vector<std::vector<jsTuple>>
runJSReference(T* t, const std::vector<std::string>& texts) {
	t->Helper();
	if (!nodeAvailable()) {
		t->Skipf("node not available", {});
	}

	// [uint32 LE count][uint32 LE len][bytes]...
	std::string in;
	auto putU32 = [&in](uint32_t v) {
		in.append(1, char(v & 0xFF));
		in.append(1, char((v >> 8) & 0xFF));
		in.append(1, char((v >> 16) & 0xFF));
		in.append(1, char((v >> 24) & 0xFF));
	};
	putU32(uint32_t(texts.size()));
	for (auto& s : texts) {
		putU32(uint32_t(s.size()));
		in.append(s);
	}

	int stdinPipe[2], stdoutPipe[2];
	if (pipe(stdinPipe) != 0 || pipe(stdoutPipe) != 0) {
		t->Fatalf("pipe failed", {});
	}
	pid_t pid;
#ifdef _WIN32
	// fork+execlp -> spawnvp: pipe ends become the child's stdin/stdout
	// (stderr inherited like the POSIX branch leaves it).
	w32::SpawnStdio io;
	io.stdinFd = stdinPipe[0];
	io.stdoutFd = stdoutPipe[1];
	std::vector<std::string> argv{"node", "-e", jsReferenceScript};
	pid = w32::spawnvp(argv, io);
	if (pid < 0) {
		t->Fatalf("spawn failed", {});
	}
#else
	pid = fork();
	if (pid < 0) {
		t->Fatalf("fork failed", {});
	}
	if (pid == 0) {
		dup2(stdinPipe[0], STDIN_FILENO);
		dup2(stdoutPipe[1], STDOUT_FILENO);
		close(stdinPipe[0]); close(stdinPipe[1]);
		close(stdoutPipe[0]); close(stdoutPipe[1]);
		execlp("node", "node", "-e", jsReferenceScript, (char*)nullptr);
		_exit(127);
	}
#endif
	close(stdinPipe[0]);
	close(stdoutPipe[1]);
	size_t off = 0;
	while (off < in.size()) {
		ssize_t n = write(stdinPipe[1], in.data() + off, in.size() - off);
		if (n <= 0) break;
		off += size_t(n);
	}
	close(stdinPipe[1]);
	std::string out;
	char buf[4096];
	ssize_t n;
	while ((n = read(stdoutPipe[0], buf, sizeof(buf))) > 0) {
		out.append(buf, size_t(n));
	}
	close(stdoutPipe[0]);
	int status = 0;
	waitpid(pid, &status, 0);
	if (status != 0) {
		t->Fatalf("node failed: status %d\nstderr: %s", {status, out});
	}

	// Parse [[{"bytePos":N,"line":N,"char":N},...],...] — the JSON is
	// machine-generated by JSON.stringify, so a fixed-shape scan suffices.
	std::vector<std::vector<jsTuple>> result;
	const char* p = out.c_str();
	auto skipWs = [&]() { while (*p == ' ' || *p == '\n') p++; };
	auto expect = [&](char c) { if (*p == c) p++; };
	auto readInt = [&]() -> int {
		int v = 0; bool neg = false;
		if (*p == '-') { neg = true; p++; }
		while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); p++; }
		return neg ? -v : v;
	};
	auto readStr = [&]() {
		expect('"');
		while (*p && *p != '"') p++;
		expect('"');
	};
	skipWs(); expect('[');
	while (true) {
		skipWs();
		if (*p == ']') { p++; break; }
		expect('[');
		std::vector<jsTuple> tuples;
		while (true) {
			skipWs();
			if (*p == ']') { p++; break; }
			expect('{');
			jsTuple tup;
			for (int f = 0; f < 3; f++) {
				skipWs(); readStr(); expect(':');
				int v = readInt();
				if (f == 0) tup.BytePos = v;
				else if (f == 1) tup.Line = v;
				else tup.Char = v;
				skipWs(); expect(',');
			}
			skipWs(); expect('}');
			tuples.push_back(tup);
			skipWs();
			expect(',');
		}
		result.push_back(std::move(tuples));
		skipWs();
		expect(',');
	}
	return result;
}

// TestConvertersAgainstJSReference cross-checks the UTF-16 conversions against
// authoritative results computed by Node.js using real UTF-16 string semantics.
static void TestConvertersAgainstJSReference(T* t) {
	t->Parallel();

	struct {
		const char* name;
		const char* text;
	} cases[] = {
	    {"empty", ""},
	    {"ascii", "hello\nworld"},
	    {"ascii_crlf", "hello\r\nworld\r\n!"},
	    {"ascii_cr_only", "a\rb\rc"},
	    {"trailing_newline", "abc\n"},
	    {"bmp_em_dash", "ab—cd\nef"},
	    {"bmp_multi", "α\nβ\nγδε\nzz"},
	    {"supplementary_emoji", "x\U0001F600y\nz"},
	    {"supplementary_at_lineend", "ab\U0001F600\ncd\U0001F60A"},
	    {"supplementary_only", "\U0001F600\U0001F601\U0001F602"},
	    {"mixed", "α — \U0001F600\r\nβ\nγ\r"},
	    {"long_mixed_ws", "  \tαβ\n\t\U0001F600  end\n"},
	    {"zwj_emoji", "\U0001F468‍\U0001F4BB\nnext"},
	    {"only_newlines", "\n\n\r\n\r"},
	};

	std::vector<std::string> texts;
	for (auto& c : cases) {
		texts.push_back(c.text);
	}
	auto refs = runJSReference(t, texts);
	gotest::assert::Equal(t, int(refs.size()), int(std::size(cases)));

	size_t i = 0;
	for (auto& c : cases) {
		auto& ref = refs[i];
		std::string text = c.text;
		t->Run(c.name, [ref, text](T* t) mutable {
			t->Parallel();

			auto [conv, script] = newTestConverters(text);
			for (auto& tup : ref) {
				TextPos bytePos(tup.BytePos);
				lsproto::Position expectedLC{
				    .Line = uint32_t(tup.Line),
				    .Character = uint32_t(tup.Char)};

				auto [gotLC, _f] = conv->ToLSPPosition(script, bytePos);
				gotest::assert::Equal(
				    t, gotLC, expectedLC,
				    gostd::sprintf(
				        "PositionToLineAndCharacter(%d) mismatch in %q",
				        {int(bytePos), text}));

				auto positions = lsconv::convertersFromLSPPosition(
				    conv, script, expectedLC, spanmap::FeatureAll);
				gotest::assert::Equal(t, int(positions.size()), 1);
				gotest::assert::Equal(
				    t, positions[0].Position, bytePos,
				    gostd::sprintf(
				        "LineAndCharacterToPosition(%d,%d) mismatch in %q",
				        {tup.Line, tup.Char, text}));
			}
		});
		i++;
	}
}
REGISTER_UNIT_TEST("ls/lsconv.TestConvertersAgainstJSReference",
                   TestConvertersAgainstJSReference);
