// tscpp — driver for the C++ port of the TypeScript compiler.
//   tscpp lex <file>           dump token stream (one per line)
//   tscpp lex-json <file>      dump token stream as JSON (oracle-comparable)
//   tscpp bench <file> [n]     lex the file n times, report throughput
//   tscpp parse <file>         parse and dump AST + diagnostics
//   tscpp bench-parse <file> [n] parse the file n times, report throughput
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "internal/ast/ast.h"
#include "internal/diagnostics/diagnostics.h"
#include "internal/parser/parser.h"
#include "internal/scanner/scanner.h"

using namespace tsc;

static std::string readFile(const char* path) {
	std::ifstream f(path, std::ios::binary);
	if (!f) {
		std::fprintf(stderr, "tscpp: cannot open %s\n", path);
		std::exit(2);
	}
	std::ostringstream ss;
	ss << f.rdbuf();
	return ss.str();
}

static void lexFile(const std::string& src, bool json) {
	Scanner s;
	s.setText(src);
	s.setSkipTrivia(false);
	s.setScriptTarget(ScriptTarget::LatestStandard);
	bool firstError = true;
	(void)firstError;
	s.onError = [&](const DiagnosticMessage* d, int pos, int length,
	                const std::vector<std::string>& args) {
		std::fprintf(stderr, "error TS%d: %d:%d %s\n", d->code, pos,
		             pos + length,
		             formatDiagnosticMessage(*d, args).c_str());
	};
	std::string out;
	char buf[256];
	bool first = true;
	if (json)
		out += '[';
	for (;;) {
		Kind t = s.scan();
		if (json) {
			if (!first)
				out += ',';
			first = false;
			std::snprintf(
				buf, sizeof(buf),
				"{\"kind\":\"%.*s\",\"pos\":%d,\"end\":%d,\"flags\":%u}",
				static_cast<int>(kindToString(t).size()),
				kindToString(t).data(), s.tokenStart(), s.tokenEnd(),
				static_cast<unsigned>(s.tokenFlags()));
			out += buf;
		} else {
			std::snprintf(buf, sizeof(buf), "%.*s\t%d\t%d\t%u\n",
			              static_cast<int>(kindToString(t).size()),
			              kindToString(t).data(), s.tokenStart(),
			              s.tokenEnd(),
			              static_cast<unsigned>(s.tokenFlags()));
			out += buf;
		}
		if (t == Kind::EndOfFile)
			break;
	}
	if (json)
		out += "]\n";
	std::fwrite(out.data(), 1, out.size(), stdout);
}

static void benchLex(const std::string& src, int iters) {
	double bestMs = 1e30;
	size_t tokens = 0;
	for (int i = 0; i < iters; i++) {
		auto t0 = std::chrono::steady_clock::now();
		Scanner s;
		s.setText(src);
		s.setScriptTarget(ScriptTarget::LatestStandard);
		tokens = 0;
		while (s.scan() != Kind::EndOfFile)
			tokens++;
		auto t1 = std::chrono::steady_clock::now();
		double ms =
			std::chrono::duration<double, std::milli>(t1 - t0).count();
		if (ms < bestMs)
			bestMs = ms;
	}
	double mb = static_cast<double>(src.size()) / (1024.0 * 1024.0);
	std::printf(
		"tscpp lex: %zu tokens, %zu bytes, best %d/%d: %.2f ms, %.1f MB/s\n",
		tokens, src.size(), iters, iters, bestMs,
		mb * 1000.0 / bestMs);
}

static ScriptKind scriptKindFromFileName(std::string_view name) {
	auto dot = name.rfind('.');
	if (dot != std::string_view::npos) {
		std::string ext(name.substr(dot));
		for (auto& c : ext)
			c = static_cast<char>(std::tolower((unsigned char)c));
		if (ext == ".js" || ext == ".cjs" || ext == ".mjs")
			return ScriptKind::JS;
		if (ext == ".jsx")
			return ScriptKind::JSX;
		if (ext == ".ts" || ext == ".cts" || ext == ".mts")
			return ScriptKind::TS;
		if (ext == ".tsx")
			return ScriptKind::TSX;
		if (ext == ".json")
			return ScriptKind::JSON;
	}
	return ScriptKind::TS;
}

static void dumpNode(Node* n, std::string& out) {
	char buf[128];
	std::snprintf(buf, sizeof(buf), "N %.*s %d %d %u\n",
	              static_cast<int>(kindToString(n->kind).size()),
	              kindToString(n->kind).data(), n->pos(), n->end(),
	              static_cast<unsigned>(n->flags));
	out += buf;
	n->forEachChild([&](Node* c) {
		dumpNode(c, out);
		return false;
	});
}

static void dumpDiags(char tag, const std::vector<Diagnostic*>& ds,
                      std::string& out) {
	for (const Diagnostic* d : ds) {
		char buf[64];
		std::snprintf(buf, sizeof(buf), "%c %d %d %d\n", tag, d->code,
		              d->loc.pos(), d->loc.end());
		out += buf;
	}
}

static void parseFile(const char* path, const std::string& src) {
	SourceFileParseOptions opts;
	opts.FileName = path;
	opts.Path = path;
	SourceFile* file = parseSourceFile(
		opts, src, scriptKindFromFileName(path));
	std::string out;
	dumpNode(file->asNode(), out);
	dumpDiags('D', file->diagnostics, out);
	dumpDiags('J', file->jsDiagnostics, out);
	dumpDiags('C', file->jsdocDiagnostics, out);
	std::fwrite(out.data(), 1, out.size(), stdout);
}

static void benchParse(const char* path, const std::string& src, int iters) {
	double bestMs = 1e30;
	for (int i = 0; i < iters; i++) {
		auto t0 = std::chrono::steady_clock::now();
		SourceFileParseOptions opts;
		opts.FileName = path;
		opts.Path = path;
		SourceFile* file = parseSourceFile(
			opts, src, scriptKindFromFileName(path));
		(void)file;
		auto t1 = std::chrono::steady_clock::now();
		double ms =
			std::chrono::duration<double, std::milli>(t1 - t0).count();
		if (ms < bestMs)
			bestMs = ms;
	}
	double mb = static_cast<double>(src.size()) / (1024.0 * 1024.0);
	std::printf(
		"tscpp parse: %zu bytes, best %d/%d: %.2f ms, %.1f MB/s\n",
		src.size(), iters, iters, bestMs, mb * 1000.0 / bestMs);
}

int main(int argc, char** argv) {
	if (argc < 3) {
		std::fprintf(
			stderr,
			"usage: tscpp <lex|lex-json|bench|parse|bench-parse> <file> [iters]\n");
		return 2;
	}
	std::string mode = argv[1];
	if (mode != "lex" && mode != "lex-json" && mode != "bench" &&
	    mode != "parse" && mode != "bench-parse") {
		std::fprintf(stderr, "tscpp: unknown mode %s\n", mode.c_str());
		return 2;
	}
	std::string src = readFile(argv[2]);
	if (mode == "lex") {
		lexFile(src, false);
	} else if (mode == "lex-json") {
		lexFile(src, true);
	} else if (mode == "bench") {
		int iters = argc > 3 ? std::atoi(argv[3]) : 5;
		benchLex(src, iters);
	} else if (mode == "parse") {
		parseFile(argv[2], src);
	} else if (mode == "bench-parse") {
		int iters = argc > 3 ? std::atoi(argv[3]) : 5;
		benchParse(argv[2], src, iters);
	} else {
		std::fprintf(stderr, "tscpp: unknown mode %s\n", mode.c_str());
		return 2;
	}
	return 0;
}
