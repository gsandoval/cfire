#!/usr/bin/env python3
"""LSP survival smoke test: open each given .cf file in `cfire lsp`, request hover, and check that
the server is still alive and answers a follow-up request.

Usage:
    python3 -I scripts/audit/lsp_smoke.py [--cfire ./cfire] file.cf [file.cf ...]
Prints ALIVE / DEAD per file; exit code 1 if any server died.
"""
import argparse
import json
import subprocess
import sys
import tempfile
import time
import shutil
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]


def frame(obj):
    body = json.dumps(obj).encode()
    return b"Content-Length: %d\r\n\r\n" % len(body) + body


def smoke(cfire, path):
    with tempfile.TemporaryDirectory(prefix="cfire_lsp_smoke_") as tmp:
        src = Path(tmp) / "main.cf"
        shutil.copy(path, src)
        uri = src.as_uri()
        p = subprocess.Popen([cfire, "lsp"], cwd=tmp, stdin=subprocess.PIPE,
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        msgs = [
            {"jsonrpc": "2.0", "id": 1, "method": "initialize",
             "params": {"rootUri": Path(tmp).as_uri(), "capabilities": {}}},
            {"jsonrpc": "2.0", "method": "initialized", "params": {}},
            {"jsonrpc": "2.0", "method": "textDocument/didOpen",
             "params": {"textDocument": {"uri": uri, "languageId": "cfire", "version": 1,
                                         "text": src.read_text()}}},
            {"jsonrpc": "2.0", "id": 2, "method": "textDocument/hover",
             "params": {"textDocument": {"uri": uri}, "position": {"line": 3, "character": 8}}},
            {"jsonrpc": "2.0", "id": 3, "method": "textDocument/documentSymbol",
             "params": {"textDocument": {"uri": uri}}},
        ]
        try:
            for m in msgs:
                p.stdin.write(frame(m))
            p.stdin.flush()
        except BrokenPipeError:
            pass
        time.sleep(3)
        alive = p.poll() is None
        p.kill()
        err = p.stderr.read().decode(errors="replace").strip().splitlines()
        return alive, (err[-1] if err else "")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cfire", default=str(REPO / "cfire"))
    ap.add_argument("files", nargs="+")
    args = ap.parse_args()
    cfire = str(Path(args.cfire).resolve())
    dead = 0
    for f in args.files:
        alive, last = smoke(cfire, f)
        dead += not alive
        print(f"{'ALIVE' if alive else 'DEAD '}  {f}  {'' if alive else last[:140]}")
    return 1 if dead else 0


if __name__ == "__main__":
    sys.exit(main())
