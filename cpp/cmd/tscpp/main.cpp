// tscpp — driver for the C++ port of the TypeScript compiler.
//   tscpp lex <file>           dump token stream (one per line)
//   tscpp lex-json <file>      dump token stream as JSON (oracle-comparable)
//   tscpp bench <file> [n]     lex the file n times, report throughput
//   tscpp parse <file>         parse and dump AST + diagnostics
//   tscpp bench-parse <file> [n] parse the file n times, report throughput
//   tscpp parse-all <dir|list> [workers]  parse many files in parallel,
//        report aggregate throughput (source files only, no dumps)
//   tscpp bind <file>          bind and dump symbols/flow/locals (oracle-comparable)
//   tscpp check <file>         semantic-check and dump diagnostics (oracle-comparable)
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <algorithm>

#include "internal/ast/ast.h"
#include "internal/ast/flow.h"
#include "internal/binder/binder.h"
#include "internal/compiler/program.h"
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
		s.setSkipTrivia(false);
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

static void dumpSymbol(const Symbol* s, std::string& out) {
	int vpos = -1, vend = -1;
	if (s->valueDeclaration) {
		vpos = s->valueDeclaration->pos();
		vend = s->valueDeclaration->end();
	}
	std::string pname = "-";
	uint32_t pflags = 0;
	if (s->parent) {
		pname = escapeAllInternalSymbolNames(symbolName(s->parent));
		pflags = static_cast<uint32_t>(s->parent->flags);
	}
	char buf[128];
	out += "S ";
	out += escapeAllInternalSymbolNames(symbolName(s));
	std::snprintf(buf, sizeof(buf), " %u %zu %d:%d ",
	              static_cast<unsigned>(s->flags), s->declarations.size(),
	              vpos, vend);
	out += buf;
	out += pname;
	std::snprintf(buf, sizeof(buf), ":%u\n", static_cast<unsigned>(pflags));
	out += buf;
}

static void dumpBindNode(Node* n, std::string& out) {
	char buf[128];
	std::snprintf(buf, sizeof(buf), "N %.*s %d %d %u\n",
	              static_cast<int>(kindToString(n->kind).size()),
	              kindToString(n->kind).data(), n->pos(), n->end(),
	              static_cast<unsigned>(n->flags));
	out += buf;
	if (n->symbol()) dumpSymbol(n->symbol(), out);
	auto e = n->exportableData();
	if (e.localSymbol && *e.localSymbol) {
		out += "X ";
		out += escapeAllInternalSymbolNames(symbolName(*e.localSymbol));
		char xbuf[32];
		std::snprintf(xbuf, sizeof(xbuf), " %u\n",
		              static_cast<unsigned>((*e.localSymbol)->flags));
		out += xbuf;
	}
	auto f = n->flowNodeData();
	if (f.flowNode && *f.flowNode) {
		FlowNode* fn = *f.flowNode;
		if (fn->node) {
			std::snprintf(buf, sizeof(buf), "F %u %.*s %d %d\n",
			              static_cast<unsigned>(fn->flags),
			              static_cast<int>(kindToString(fn->node->kind).size()),
			              kindToString(fn->node->kind).data(), fn->node->pos(),
			              fn->node->end());
		} else {
			std::snprintf(buf, sizeof(buf), "F %u -\n",
			              static_cast<unsigned>(fn->flags));
		}
		out += buf;
	}
	auto b = n->bodyData();
	if (b.endFlowNode && *b.endFlowNode) {
		std::snprintf(buf, sizeof(buf), "E %u\n",
		              static_cast<unsigned>((*b.endFlowNode)->flags));
		out += buf;
	}
	FlowNode* ret = nullptr;
	switch (n->kind) {
	case Kind::Constructor:
		ret = n->as<ConstructorDeclaration>()->ReturnFlowNode;
		break;
	case Kind::FunctionDeclaration:
		ret = n->as<FunctionDeclaration>()->ReturnFlowNode;
		break;
	case Kind::FunctionExpression:
		ret = n->as<FunctionExpression>()->ReturnFlowNode;
		break;
	case Kind::ClassStaticBlockDeclaration:
		ret = n->as<ClassStaticBlockDeclaration>()->ReturnFlowNode;
		break;
	default:
		break;
	}
	if (ret) {
		std::snprintf(buf, sizeof(buf), "R %u\n",
		              static_cast<unsigned>(ret->flags));
		out += buf;
	}
	auto c = n->localsContainerData();
	if (c.locals && !c.locals->empty()) {
		std::vector<std::string> keys;
		for (auto& [k, v] : *c.locals) keys.push_back(k);
		std::sort(keys.begin(), keys.end());
		out += "L ";
		for (auto& k : keys) {
			char fbuf[512];
			std::snprintf(fbuf, sizeof(fbuf), "%s:%u;",
			              escapeAllInternalSymbolNames(k).c_str(),
			              static_cast<unsigned>((*c.locals)[k]->flags));
			out += fbuf;
		}
		out.pop_back();
		out += "\n";
	}
	if (c.nextContainer && *c.nextContainer) {
		std::snprintf(buf, sizeof(buf), "Q %d %d\n",
		              (*c.nextContainer)->pos(), (*c.nextContainer)->end());
		out += buf;
	}
	n->forEachChild([&](Node* ch) {
		dumpBindNode(ch, out);
		return false;
	});
}

static void bindFile(const char* path, const std::string& src) {
	SourceFileParseOptions opts;
	opts.FileName = path;
	opts.Path = path;
	SourceFile* file = parseSourceFile(opts, src, scriptKindFromFileName(path));
	bindSourceFile(file);
	std::string out;
	dumpBindNode(file->asNode(), out);
	char buf[64];
	std::snprintf(buf, sizeof(buf), "M %d %d\n",
	              file->CommonJSModuleIndicator != nullptr ? 1 : 0,
	              file->ExternalModuleIndicator != nullptr ? 1 : 0);
	out += buf;
	std::snprintf(buf, sizeof(buf), "K %d\n", file->SymbolCount);
	out += buf;
	for (auto* p : file->PatternAmbientModules) {
		char pbuf[1024];
		std::snprintf(pbuf, sizeof(pbuf), "P %s %s:%u\n", p->pattern.c_str(),
		              escapeAllInternalSymbolNames(symbolName(p->symbol)).c_str(),
		              static_cast<unsigned>(p->symbol->flags));
		out += pbuf;
	}
	std::vector<std::string> gkeys;
	for (auto& [k, v] : file->GlobalExports) gkeys.push_back(k);
	std::sort(gkeys.begin(), gkeys.end());
	for (auto& k : gkeys) {
		char gbuf[512];
		std::snprintf(gbuf, sizeof(gbuf), "G %s %u\n",
		              escapeAllInternalSymbolNames(k).c_str(),
		              static_cast<unsigned>(file->GlobalExports[k]->flags));
		out += gbuf;
	}
	dumpDiags('B', file->bindDiagnostics, out);
	std::fwrite(out.data(), 1, out.size(), stdout);
}

// checkFile — `tsc --noEmit <file>` equivalent. Emits the canonical
// diagnostic dump that checkdump (Go) produces:
//   G <code>                file-less diagnostics
//   F <fileName>            per file that has diagnostics
//   T <code> <pos> <end>    diagnostics in that file
// Mirrors tsc/cmd/checkdump/main.go exactly (see that file for the format).
static std::string findBundledLibsRoot() {
	namespace fs = std::filesystem;
	// Locate tsc/internal/bundled/libs relative to the executable, then CWD.
	fs::path exe = fs::canonical("/proc/self/exe");
	for (fs::path dir = exe.parent_path(); !dir.empty();
	     dir = dir.parent_path()) {
		fs::path cand = dir / "tsc" / "internal" / "bundled" / "libs";
		if (fs::is_directory(cand))
			return cand.string();
		if (dir == dir.root_path())
			break;
	}
	fs::path cwd = fs::current_path() / "tsc" / "internal" / "bundled" / "libs";
	if (fs::is_directory(cwd))
		return cwd.string();
	return "";
}

// Canonical dump (checkdump/main.go) — shared by check and emit modes.
static void dumpDiagnostics(const std::vector<Diagnostic*>& diags) {
	// Canonical dump (checkdump/main.go).
	std::unordered_map<SourceFile*, std::vector<Diagnostic*>> byFile;
	std::vector<Diagnostic*> global;
	for (auto* d : diags) {
		if (d->File() != nullptr) {
			byFile[d->File()].push_back(d);
		} else {
			global.push_back(d);
		}
	}
	std::sort(global.begin(), global.end(),
	          [](Diagnostic* a, Diagnostic* b) {
		          return a->Code() < b->Code();
	          });
	std::string out;
	char buf[64];
	for (auto* d : global) {
		std::snprintf(buf, sizeof(buf), "G %d\n", d->Code());
		out += buf;
	}
	std::vector<std::string> names;
	std::unordered_map<std::string, SourceFile*> nameToFile;
	for (auto& [f, ds] : byFile) {
		names.push_back(f->FileName());
		nameToFile[f->FileName()] = f;
	}
	std::sort(names.begin(), names.end());
	for (auto& name : names) {
		out += "F " + name + "\n";
		auto& ds = byFile[nameToFile[name]];
		std::sort(ds.begin(), ds.end(), [](Diagnostic* a, Diagnostic* b) {
			if (a->Pos() != b->Pos())
				return a->Pos() < b->Pos();
			if (a->End() != b->End())
				return a->End() < b->End();
			return a->Code() < b->Code();
		});
		for (auto* d : ds) {
			std::snprintf(buf, sizeof(buf), "T %d %d %d\n", d->Code(),
			              d->Pos(), d->End());
			out += buf;
		}
	}
	std::fwrite(out.data(), 1, out.size(), stdout);
}

static void checkFile(int argc, char** argv) {
	compiler::CompilerHost host;
	host.currentDirectory =
	    tspath::normalizePath(std::filesystem::current_path().string());
	host.bundledLibsRoot = findBundledLibsRoot();

	// `tsc --noEmit <argv[2:]>` — "check" is our own subcommand, not a root
	// filename (checkdump is invoked as `checkdump <file>`); every
	// remaining arg becomes a root file via ParseCommandLine.
	std::vector<std::string> rootFileNames;
	for (int i = 2; i < argc; i++) {
		rootFileNames.push_back(argv[i]);
	}

	// ParseCommandLine defaults for bare file args: NoEmit set, everything
	// else at defaults (no config file).
	CompilerOptions options;
	options.NoEmit = Tristate::True;

	compiler::SimpleProgram program(&host, options, rootFileNames);
	program.BindSourceFiles();
	auto diags = compiler::getDiagnosticsOfAnyProgram(&program, {}, false);

	dumpDiagnostics(diags);

	// Verbose diagnostics (TSC_FULL_DIAGS=1): formatted text + chains for
	// divergence triage. Not part of the oracle-aligned dump.
	if (std::getenv("TSC_FULL_DIAGS") != nullptr) {
		std::string v;
		std::function<void(const Diagnostic*, int)> dump =
		    [&](const Diagnostic* d, int depth) {
			    for (int i = 0; i < depth; i++) v += "  ";
			    char hb[512];
			    const std::string fn =
			        d->File() != nullptr ? d->File()->FileName() : "";
			    std::snprintf(hb, sizeof(hb), "TS%d %s:%d:%d ", d->Code(),
			                  fn.c_str(), d->Pos(), d->End());
			    v += hb;
			    if (!d->MessageText().empty()) {
				    v += d->MessageText();
			    } else if (d->message != nullptr) {
				    v += formatDiagnosticMessage(*d->message,
				                                 d->MessageArgs());
			    }
			    v += '\n';
			    for (auto* c : d->messageChain) dump(c, depth + 1);
			    for (auto* r : d->relatedInformation) dump(r, depth + 1);
		    };
		for (auto* d : diags) dump(d, 0);
		std::fwrite(v.data(), 1, v.size(), stderr);
	}
}

// emitFile — `tsc <file>` equivalent (default emit). Runs the full program
// pipeline (pre-emit diagnostics, then Emit writes .js/.d.ts via the host
// WriteFile) and dumps every diagnostic in the canonical checkdump format.
static void emitFile(int argc, char** argv) {
	compiler::CompilerHost host;
	host.currentDirectory =
	    tspath::normalizePath(std::filesystem::current_path().string());
	host.bundledLibsRoot = findBundledLibsRoot();

	std::vector<std::string> rootFileNames;
	for (int i = 2; i < argc; i++) {
		rootFileNames.push_back(argv[i]);
	}

	// `tsc <argv[2:]>` — no NoEmit; defaults like a bare file-args
	// command line (execute/tsc.go).
	CompilerOptions options;

	compiler::SimpleProgram program(&host, options, rootFileNames);
	program.BindSourceFiles();
	auto diags = compiler::getDiagnosticsOfAnyProgram(&program, {}, false);

	compiler::EmitOptions emitOptions;
	compiler::EmitResult* emitResult = program.Emit(&emitOptions);
	if (emitResult != nullptr) {
		diags.insert(diags.end(), emitResult->Diagnostics.begin(),
		             emitResult->Diagnostics.end());
	}
	dumpDiagnostics(diags);
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

static void parseAll(const char* path, int workers) {
	namespace fs = std::filesystem;
	std::vector<std::string> files;
	fs::path p(path);
	if (fs::is_directory(p)) {
		for (auto& e : fs::recursive_directory_iterator(p)) {
			if (!e.is_regular_file())
				continue;
			auto ext = e.path().extension().string();
			for (auto& c : ext)
				c = static_cast<char>(std::tolower((unsigned char)c));
			if (ext == ".ts" || ext == ".tsx" || ext == ".d.ts" ||
			    ext == ".mts" || ext == ".cts" || ext == ".js" ||
			    ext == ".jsx" || ext == ".mjs" || ext == ".cjs") {
				files.push_back(e.path().string());
			}
		}
	} else {
		// A text file with one path per line.
		std::ifstream in(path);
		std::string line;
		while (std::getline(in, line)) {
			if (!line.empty())
				files.push_back(line);
		}
	}
	if (workers <= 0)
		workers = static_cast<int>(std::thread::hardware_concurrency());
	std::atomic<size_t> next{0};
	std::atomic<size_t> totalBytes{0};
	auto t0 = std::chrono::steady_clock::now();
	// Worker threads default to small stacks (512KB on macOS); the parser
	// recurses per AST nesting level, so give each worker a stack sized like
	// the main thread's to keep deep files safe.
	struct Work {
		const std::vector<std::string>* files;
		std::atomic<size_t>* next;
		std::atomic<size_t>* totalBytes;
	};
	auto workerMain = [](void* ctx) -> void* {
		auto* work = static_cast<Work*>(ctx);
		for (;;) {
			size_t i = work->next->fetch_add(1);
			if (i >= work->files->size())
				break;
			const std::string& path = (*work->files)[i];
			std::string src = readFile(path.c_str());
			SourceFileParseOptions opts;
			opts.FileName = path;
			opts.Path = path;
			SourceFile* f = parseSourceFile(
				opts, src, scriptKindFromFileName(path));
			(void)f;
			*work->totalBytes += src.size();
		}
		return nullptr;
	};
	Work work{&files, &next, &totalBytes};
	std::vector<pthread_t> pool(static_cast<size_t>(workers));
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setstacksize(&attr, size_t{64} << 20);
	for (int w = 0; w < workers; w++) {
		pthread_create(&pool[static_cast<size_t>(w)], &attr, workerMain,
		               &work);
	}
	pthread_attr_destroy(&attr);
	for (auto& t : pool)
		pthread_join(t, nullptr);
	double ms =
		std::chrono::duration<double, std::milli>(
			std::chrono::steady_clock::now() - t0)
			.count();
	double mb = static_cast<double>(totalBytes) / (1024.0 * 1024.0);
	std::printf(
		"tscpp parse-all: %zu files, %.1f MB, %d workers: %.1f ms, %.1f MB/s\n",
		files.size(), mb, workers, ms, mb * 1000.0 / ms);
}

int main(int argc, char** argv) {
	if (argc < 3) {
		std::fprintf(
			stderr,
			"usage: tscpp <lex|lex-json|bench|parse|bench-parse|parse-all|bind|check|emit> <file|dir> [iters|workers]\n");
		return 2;
	}
	std::string mode = argv[1];
	if (mode != "lex" && mode != "lex-json" && mode != "bench" &&
	    mode != "parse" && mode != "bench-parse" && mode != "parse-all" &&
	    mode != "bind" && mode != "check" && mode != "emit") {
		std::fprintf(stderr, "tscpp: unknown mode %s\n", mode.c_str());
		return 2;
	}
	if (mode == "parse-all") {
		int workers = argc > 3 ? std::atoi(argv[3]) : 0;
		parseAll(argv[2], workers);
		return 0;
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
	} else if (mode == "bind") {
		bindFile(argv[2], src);
	} else if (mode == "check") {
		checkFile(argc, argv);
	} else if (mode == "emit") {
		emitFile(argc, argv);
	} else if (mode == "bench-parse") {
		int iters = argc > 3 ? std::atoi(argv[3]) : 5;
		benchParse(argv[2], src, iters);
	} else {
		std::fprintf(stderr, "tscpp: unknown mode %s\n", mode.c_str());
		return 2;
	}
	return 0;
}
