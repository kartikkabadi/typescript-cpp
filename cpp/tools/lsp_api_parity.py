#!/usr/bin/env python3
"""E2E parity driver: runs a scripted JSON-RPC session against two binaries and diffs transcripts.

Usage:
  lsp_api_parity.py lsp <go_bin> <cpp_bin> [workdir]
  lsp_api_parity.py api <go_bin> <cpp_bin> [workdir]

Prints a unified diff (Go transcript vs C++ transcript); exits 0 iff identical.
"""
import sys, os, json, difflib
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rpcframe import RpcSession, uri_for, transcript_lines

SAMPLE = """// sample project for lsp parity
export interface Shape { kind: string; area(): number }
export class Circle implements Shape {
    kind = "circle" as const;
    constructor(public radius: number) {}
    area() { return Math.PI * this.radius ** 2 }
}
const c = new Circle(2);
c.area();
function add(a: number, b: number) { return a + b }
const total = add(c.radius, 1);
"""

def lsp_scenario(workdir):
    path = os.path.join(workdir, "a.ts")
    with open(path, "w") as f:
        f.write(SAMPLE)
    uri = uri_for(path)
    s = RpcSession([BIN, "--lsp", "--stdio"], cwd=workdir)
    s.call("initialize", {
        "processId": None, "rootUri": uri_for(workdir),
        "capabilities": {"textDocument": {
            "completion": {"completionItem": {"snippetSupport": True}},
            "publishDiagnostics": {"relatedInformation": True},
            "semanticTokens": {"requests": {"full": True}, "formats": ["relative"],
                               "tokenTypes": ["class", "interface", "function", "variable"],
                               "tokenModifiers": []}},
            "workspace": {"configuration": True}},
        "clientInfo": {"name": "parity-probe", "version": "0"},
    }, timeout=60)
    s.notify("initialized", {})
    s.notify("textDocument/didOpen", {"textDocument": {
        "uri": uri, "languageId": "typescript", "version": 1, "text": SAMPLE}})
    s.drain(1.0)  # let publishDiagnostics flow (skipped in transcript anyway)
    for method, params in [
        ("textDocument/completion", {"textDocument": {"uri": uri},
         "position": {"line": 9, "character": 2}, "context": {"triggerKind": 1}}),
        ("textDocument/hover", {"textDocument": {"uri": uri},
         "position": {"line": 8, "character": 8}}),
        ("textDocument/definition", {"textDocument": {"uri": uri},
         "position": {"line": 8, "character": 8}}),
        ("textDocument/references", {"textDocument": {"uri": uri},
         "position": {"line": 6, "character": 10}, "context": {"includeDeclaration": True}}),
        ("textDocument/documentSymbol", {"textDocument": {"uri": uri}}),
        ("textDocument/signatureHelp", {"textDocument": {"uri": uri},
         "position": {"line": 11, "character": 19}, "context": {"triggerKind": 1, "isRetrigger": False}}),
        ("textDocument/formatting", {"textDocument": {"uri": uri},
         "options": {"tabSize": 4, "insertSpaces": True}}),
        ("textDocument/rename", {"textDocument": {"uri": uri},
         "position": {"line": 6, "character": 10}, "newName": "myCircle"}),
    ]:
        s.call(method, params, timeout=60)
    s.call("shutdown", timeout=30)
    s.notify("exit")
    s.drain(0.5)
    s.kill()
    return s


def api_scenario(workdir):
    path = os.path.join(workdir, "a.ts")
    with open(path, "w") as f:
        f.write(SAMPLE)
    s = RpcSession([BIN, "--api", "--async"], cwd=workdir)
    s.call("echo", {"hello": "world", "n": 42}, timeout=30)
    s.call("ping", timeout=30)
    s.call("initialize", {"capabilities": {}}, timeout=60)
    s.call("transpileModule", {"fileName": uri_for(path), "text": SAMPLE,
                               "compilerOptions": {"target": "es2020", "module": "commonjs"}},
           timeout=60)
    s.call("transpileDeclaration", {"fileName": uri_for(path), "text": SAMPLE,
                                    "compilerOptions": {"strict": True}}, timeout=60)
    s.call("parseCommandLine", {"commandLine": ["a.ts", "--noEmit", "--strict"],
                                "currentDirectory": workdir}, timeout=60)
    s.kill()
    return s


if __name__ == "__main__":
    mode, go_bin, cpp_bin = sys.argv[1], sys.argv[2], sys.argv[3]
    workdir = sys.argv[4] if len(sys.argv) > 4 else "/tmp/parity_work"
    os.makedirs(workdir, exist_ok=True)

    results = {}
    for tag, binpath in (("GO", go_bin), ("CPP", cpp_bin)):
        BIN = binpath
        globals()["BIN"] = binpath
        try:
            sess = lsp_scenario(workdir) if mode == "lsp" else api_scenario(workdir)
            results[tag] = transcript_lines(sess.log)
            with open(f"/tmp/{mode}_{tag}.stderr.txt", "w") as f:
                f.write("\n".join(sess.err))
        except Exception as e:
            results[tag] = [f"SESSION-FAILED: {e}"]
    with open(f"/tmp/{mode}_GO.log", "w") as f:
        f.write("\n".join(results["GO"]))
    with open(f"/tmp/{mode}_CPP.log", "w") as f:
        f.write("\n".join(results["CPP"]))
    diff = list(difflib.unified_diff(results["GO"], results["CPP"],
                                   "go", "cpp", lineterm=""))
    if diff:
        print("\n".join(diff[:200]))
        print(f"--- DIFF: {mode} transcripts differ ({len(diff)} diff lines)")
        sys.exit(1)
    print(f"PASS: {mode} transcripts identical ({len(results['GO'])} events)")
    sys.exit(0)
