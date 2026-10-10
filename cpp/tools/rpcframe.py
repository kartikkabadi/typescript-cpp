"""Shared Content-Length JSON-RPC framing + session driver for tsc --lsp/--api e2e tests."""
import json, subprocess, sys, threading, queue, os

def encode(msg):
    body = json.dumps(msg, separators=(",", ":")).encode()
    return b"Content-Length: %d\r\n\r\n" % len(body) + body

class RpcSession:
    """Drives one stdio JSON-RPC peer (tsc --lsp --stdio or tsc --api --async)."""
    def __init__(self, argv, cwd=None):
        self.proc = subprocess.Popen(argv, stdin=subprocess.PIPE,
                                     stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                     cwd=cwd)
        self.q = queue.Queue()
        self.err = []
        threading.Thread(target=self._reader, daemon=True).start()
        threading.Thread(target=self._errreader, daemon=True).start()
        self.next_id = 0
        self.log = []          # transcript: ("C", msg) client->server, ("S", msg) server->client
        self.notifs = []

    def _reader(self):
        buf = b""
        f = self.proc.stdout
        while True:
            # read headers
            headers = {}
            line = f.readline()
            if not line:
                self.q.put(None); return
            while line not in (b"\r\n", b"\n", b""):
                k, _, v = line.decode().partition(":")
                headers[k.strip().lower()] = v.strip()
                line = f.readline()
            n = int(headers.get("content-length", "0"))
            if n <= 0:
                continue
            body = f.read(n)
            try:
                self.q.put(json.loads(body))
            except Exception:
                self.q.put({"_raw": body.decode("utf-8", "replace")})

    def _errreader(self):
        for line in self.proc.stderr:
            self.err.append(line.decode("utf-8", "replace").rstrip())

    def send(self, method, params=None):
        self.next_id += 1
        msg = {"jsonrpc": "2.0", "id": self.next_id, "method": method}
        if params is not None:
            msg["params"] = params
        self.proc.stdin.write(encode(msg)); self.proc.stdin.flush()
        self.log.append(("C", msg))
        return self.next_id

    def notify(self, method, params=None):
        msg = {"jsonrpc": "2.0", "method": method}
        if params is not None:
            msg["params"] = params
        self.proc.stdin.write(encode(msg)); self.proc.stdin.flush()
        self.log.append(("C", msg))

    def _handle_server_request(self, msg):
        """Server->client request (has id + method): respond so the server doesn't block."""
        mid, method = msg.get("id"), msg.get("method", "")
        if method == "workspace/configuration":
            items = (msg.get("params") or {}).get("items") or []
            result = [None] * len(items)
        elif method == "window/showMessageRequest":
            result = None
        else:
            # client/registerCapability, client/unregisterCapability,
            # workspace/workDoneProgress/create, window/workDoneProgress/create, ...
            result = None
        resp = {"jsonrpc": "2.0", "id": mid, "result": result}
        try:
            self.proc.stdin.write(encode(resp)); self.proc.stdin.flush()
        except Exception:
            pass
        self.log.append(("R", {"_method": method, "_id": mid, "result": result}))

    def wait_response(self, req_id, timeout=30):
        import time
        deadline = time.time() + timeout
        while True:
            try:
                msg = self.q.get(timeout=max(0.1, deadline - time.time()))
            except queue.Empty:
                raise TimeoutError(f"no response for id={req_id}")
            if msg is None:
                raise EOFError("server closed stdout")
            self.log.append(("S", msg))
            if "id" in msg and "method" in msg:
                self._handle_server_request(msg)
                continue
            if msg.get("id") == req_id:
                return msg
            self.notifs.append(msg)

    def call(self, method, params=None, timeout=30):
        return self.wait_response(self.send(method, params), timeout)

    def drain(self, secs=0.5):
        import time
        end = time.time() + secs
        while time.time() < end:
            try:
                msg = self.q.get(timeout=max(0.05, end - time.time()))
            except queue.Empty:
                break
            if msg is None:
                break
            self.log.append(("S", msg))
            if "id" in msg and "method" in msg:
                self._handle_server_request(msg)
            else:
                self.notifs.append(msg)

    def kill(self):
        try:
            self.proc.stdin.close()
        except Exception:
            pass
        self.proc.terminate()
        try:
            self.proc.wait(timeout=5)
        except Exception:
            self.proc.kill()

def uri_for(path):
    # as_uri() percent-encodes URI-reserved chars ('#', '%', spaces) that
    # would otherwise fragment/alter the document URI.
    from pathlib import Path
    return Path(path).resolve().as_uri()

def normalize(obj, _key=""):
    """Recursively normalize volatile bits for byte-diff.

    Go map-iteration order makes some result arrays non-deterministic run-to-run
    (verified: completion items, two identical Go runs differ). Sort only the
    array kinds where order is provably a set, never a sequence."""
    if isinstance(obj, dict):
        out = {k: normalize(v, k) for k, v in sorted(obj.items())}
        if _key == "result" and "items" in out and isinstance(out["items"], list):
            items = out["items"]
            if all(isinstance(i, dict) and "label" in i for i in items):
                out["items"] = sorted(items, key=lambda i: json.dumps(i, sort_keys=True))
        return out
    if isinstance(obj, list):
        return [normalize(v, _key) for v in obj]
    if isinstance(obj, str):
        # Backslash→slash only for path/URI-valued fields; blanket
        # conversion would equate distinct text values like C:\foo.
        if _PATH_KEY.search(_key):
            return obj.replace("\\", "/")
        return obj
    return obj

_PATH_KEY = __import__("re").compile(
    r"uri|file|path|dir|folder|root|location|target", __import__("re").I)

def transcript_lines(log, skip_notifs=("$/", "window/logMessage", "telemetry/",
                                        "textDocument/publishDiagnostics")):
    out = []
    for side, msg in log:
        m = msg.get("method", "")
        if side == "S" and any(m.startswith(p) for p in skip_notifs):
            continue  # server notifications ($/progress, log/trace, window/logMessage timings)
        out.append(side + " " + json.dumps(normalize(msg), separators=(",", ":"), sort_keys=True))
    return out
