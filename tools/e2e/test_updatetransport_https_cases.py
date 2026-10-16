#!/usr/bin/env python3
"""Checks how test_updatetransport_https reports each kind of answer. No external network.

usage: test_updatetransport_https_cases.py PROBE

The probe asks a server on 127.0.0.1 for its address. The answers stand for GitHub: a release list, a rate limit, an
outage and a wrong address. A connection that the server closes without a response stands for one that cannot be made.
The probe must pass only for a release list, skip for a rate limit or an outage, and fail with a text that names the
cause: the missing response or the HTTP status.
"""
import http.server
import json
import socket
import subprocess
import sys
import threading

STABLE = [{"tag_name": "v1.2.3", "draft": False, "prerelease": False, "assets": []}]
PRERELEASE = [{"tag_name": "v1.3.0-rc1", "draft": False, "prerelease": True, "assets": []}]

# path: (status, body, exit code of the probe, text of its output)
CASES = {
    "/stable": (200, json.dumps(STABLE), 0, "PASS The server answered HTTP 200 over HTTPS and the newest stable release is v1.2.3."),
    "/prerelease": (200, json.dumps(PRERELEASE), 0, "PASS The server answered HTTP 200 over HTTPS with a release list that has no stable release"),
    "/portal": (200, "<html>Sign in to the network</html>", 1, "FAIL The server answered HTTP 200 over HTTPS, but the body is not a release list"),
    "/rate-limit": (403, '{"message":"API rate limit exceeded"}', 77, "SKIP The server answered HTTP 403 over HTTPS, probably its rate limit"),
    "/too-many": (429, "", 77, "SKIP The server answered HTTP 429 over HTTPS, probably its rate limit"),
    "/outage": (503, "", 77, "SKIP The server answered HTTP 503 over HTTPS, probably an outage"),
    "/missing": (404, '{"message":"Not Found"}', 1, "FAIL The server answered HTTP 404 over HTTPS, not a release list. This is not a TLS failure"),
    "/drop": (0, "", 1, "FAIL No HTTP response arrived, so the connection, the TLS handshake or the certificate check failed"),
}


class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self) -> None:
        status, body = CASES[self.path][:2]
        if status == 0:
            self.request.shutdown(socket.SHUT_RDWR)
            return
        data = body.encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def log_message(self, *args) -> None:
        pass


def run_probe(probe: str, url: str) -> tuple[int, str]:
    result = subprocess.run([probe, url], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=90)
    return result.returncode, result.stdout.strip()


def main() -> int:
    probe = sys.argv[1]
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    failures = []
    try:
        for path, (_status, _body, code, text) in CASES.items():
            actual_code, output = run_probe(probe, f"http://127.0.0.1:{server.server_port}{path}")
            if actual_code != code or not output.startswith(text):
                failures.append(f"{path}: expected exit {code} and '{text}', got exit {actual_code} and '{output}'")
    finally:
        server.shutdown()
        server.server_close()
    for failure in failures:
        print(failure)
    if failures:
        return 1
    print(f"PASS {len(CASES)} answers reported as expected")
    return 0


if __name__ == "__main__":
    sys.exit(main())
