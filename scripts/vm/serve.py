# scripts/vm/serve.py — serves the VM share folder over HTTP on the host-only address and accepts
# `PUT /results/<name>` uploads, which is how the guests report back (utmctl's own output capture races the
# guest process). Usage: python3 serve.py <share-dir> <bind-ip> <port>
import http.server
import os
import sys

ROOT, HOST, PORT = sys.argv[1], sys.argv[2], int(sys.argv[3])
os.makedirs(os.path.join(ROOT, "results"), exist_ok=True)


class Handler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *a, **k):
        super().__init__(*a, directory=ROOT, **k)

    def do_PUT(self):
        name = os.path.basename(self.path)
        if not self.path.startswith("/results/") or not name or name.startswith("."):
            self.send_response(403)
            self.end_headers()
            return
        data = self.rfile.read(int(self.headers.get("Content-Length", 0)))
        with open(os.path.join(ROOT, "results", name), "wb") as f:
            f.write(data)
        self.send_response(201)
        self.end_headers()

    def log_message(self, *a):
        pass


http.server.ThreadingHTTPServer((HOST, PORT), Handler).serve_forever()
