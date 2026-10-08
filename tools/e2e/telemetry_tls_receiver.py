#!/usr/bin/env python3
"""Isolated numeric-loopback TLS transport regression. No external network."""
import json
import socket
import ssl
import subprocess
import sys
import threading
from pathlib import Path


def run_case(client: str, cert: str, key: str, ca: str, scenario: int) -> None:
    listener = socket.socket()
    listener.bind(("127.0.0.1", 0))
    listener.listen(4)
    listener.settimeout(0.2)
    port = listener.getsockname()[1]
    tls = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    tls.load_cert_chain(cert, key)
    requests = []
    finished = threading.Event()

    def serve() -> None:
        while not finished.is_set():
            try:
                conn, _ = listener.accept()
            except socket.timeout:
                continue
            except OSError:
                return
            try:
                with tls.wrap_socket(conn, server_side=True) as stream:
                    stream.settimeout(4)
                    data = b""
                    while b"\r\n\r\n" not in data and len(data) < 65536:
                        chunk = stream.recv(8192)
                        if not chunk:
                            break
                        data += chunk
                    if b"\r\n\r\n" not in data:
                        continue
                    header, body = data.split(b"\r\n\r\n", 1)
                    length = next((int(line.split(b":", 1)[1]) for line in header.split(b"\r\n")
                                   if line.lower().startswith(b"content-length:")), 0)
                    while len(body) < length:
                        chunk = stream.recv(min(8192, length - len(body)))
                        if not chunk:
                            break
                        body += chunk
                    requests.append((header, body))
                    if scenario == 2:
                        response = (f"HTTP/1.1 302 Found\r\nLocation: https://127.0.0.1:{port}/leak\r\n"
                                    "Content-Length: 0\r\nConnection: close\r\n\r\n").encode()
                    else:
                        response = b"HTTP/1.1 202 Accepted\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"
                    stream.sendall(response)
            except (ssl.SSLError, ConnectionError, TimeoutError):
                pass
            finally:
                conn.close()

    thread = threading.Thread(target=serve, daemon=True)
    thread.start()
    try:
        result = subprocess.run([client, cert, str(port), str(scenario), ca], timeout=20, capture_output=True, text=True,
                                encoding="utf-8", errors="replace")
        if result.returncode == 77 and scenario != 0 and "UNSUPPORTED: Qt5 Secure Transport" in result.stderr:
            print(result.stderr.strip(), file=sys.stderr)
            raise SystemExit(77)
        if result.returncode:
            raise AssertionError(f"scenario {scenario} client failed: requests={len(requests)} {result.stdout} {result.stderr}")
        expected = 0 if scenario == 0 else 2  # Trusted preflight plus sender POST; no redirect follow.
        if len(requests) != expected:
            raise AssertionError(f"scenario {scenario}: expected {expected} HTTP requests, got {len(requests)}")
        if requests:
            if not all(header.startswith(b"POST /collect HTTP/1.1") for header, _ in requests):
                raise AssertionError("trusted request missing expected POST")
            queued = [line.removeprefix("TLS_TEST_QUEUED_EVENT=") for line in result.stdout.splitlines()
                      if line.startswith("TLS_TEST_QUEUED_EVENT=")]
            body = requests[-1][1]
            batch = json.loads(body)
            if (len(queued) != 1 or len(body) > 65536 or batch.get("schema_version") != 1
                    or batch.get("category") != "usage" or batch.get("events") != [json.loads(queued[0])]):
                raise AssertionError("trusted wire event differs from the original durable queued event")
    finally:
        finished.set()
        listener.close()
        thread.join(timeout=2)


def main() -> None:
    if len(sys.argv) != 6 or sys.argv[5] not in ("reject", "trusted"):
        raise SystemExit("usage: telemetry_tls_receiver.py client cert.pem key.pem ca.pem reject|trusted")
    client, cert, key, ca = (str(Path(path).resolve()) for path in sys.argv[1:5])
    for scenario in ((0,) if sys.argv[5] == "reject" else (1, 2)):
        run_case(client, cert, key, ca, scenario)
    print("numeric-loopback TLS:", "untrusted certificate rejected" if sys.argv[5] == "reject"
          else "trusted 202 accepted; redirect not followed")


if __name__ == "__main__":
    main()
